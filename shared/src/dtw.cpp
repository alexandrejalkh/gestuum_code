/**
 * ============================================================
 * GESTUUM — Dynamic Time Warping Implementation
 * ============================================================
 * Compara trajetorias 3D capturadas dos sensores IMU contra
 * trajetorias de referencia do banco de gestos.
 *
 * Algoritmo DTW: encontra o alinhamento otimo entre duas
 * sequencias de pontos 3D, tolerando variacoes de velocidade.
 * Score menor = mais similar. Score < threshold = match.
 *
 * Memoria: usa rolling array de 2 linhas ao inves de matriz
 * completa. Reduz consumo de ~91KB para ~1.2KB no ESP32.
 * ============================================================
 */

#include "dtw.h"
#include "constants.h"
#include <cfloat>
#include <cmath>
#include <algorithm>

DTW::DTW()
    : lastBestScore(FLT_MAX) {
}

/**
 * Distancia euclidiana entre dois pontos 3D no grid 11x11x11.
 * Cada coordenada e int8_t [0-10], entao dx/dy/dz max = 10.
 * Distancia maxima possivel = sqrt(300) ≈ 17.3
 */
float DTW::pointDistance(const Point3D& p1, const Point3D& p2) {
    float dx = static_cast<float>(p1.x - p2.x);
    float dy = static_cast<float>(p1.y - p2.y);
    float dz = static_cast<float>(p1.z - p2.z);
    return sqrt(dx * dx + dy * dy + dz * dz);
}

/**
 * Calcula distancia DTW entre duas trajetorias 3D.
 *
 * FIX C01: Substituido vector-of-vectors (~91KB no heap, 151 alocacoes
 * separadas que fragmentam memoria) por rolling array de 2 linhas
 * (~1.2KB). ESP32 tem ~160KB livres — a versao anterior podia causar
 * abort() por falta de memoria contigua.
 *
 * Retorna score normalizado por (N+M). Menor = mais similar.
 * Retorna FLT_MAX se alguma trajetoria esta vazia ou excede limite.
 */
float DTW::calculate(const std::vector<Point3D>& t1,
                     const std::vector<Point3D>& t2) {
    int N = static_cast<int>(t1.size());
    int M = static_cast<int>(t2.size());

    // Trajetorias vazias nao podem ser comparadas
    if (N == 0 || M == 0) {
        return FLT_MAX;
    }

    // Limite de tamanho para evitar alocacao excessiva
    if (N > DTW_MAX_COST_MATRIX_SIZE || M > DTW_MAX_COST_MATRIX_SIZE) {
        return FLT_MAX;
    }

    // FIX C01: Rolling array — so precisa da linha anterior e da atual
    // Em vez de alocar matriz (N+1)x(M+1) inteira (~91KB),
    // usamos apenas 2 linhas de (M+1) floats (~1.2KB)
    std::vector<float> prev(M + 1, FLT_MAX);
    std::vector<float> curr(M + 1, FLT_MAX);

    // Caso base: custo zero na origem
    prev[0] = 0.0f;

    // Preenche a matriz DTW linha por linha, mantendo so 2 linhas
    for (int i = 1; i <= N; i++) {
        // Reset da linha atual (cada nova linha comeca com FLT_MAX)
        std::fill(curr.begin(), curr.end(), FLT_MAX);

        for (int j = 1; j <= M; j++) {
            // Custo de alinhar t1[i-1] com t2[j-1]
            float cost = pointDistance(t1[i - 1], t2[j - 1]);

            // Tres opcoes: insercao, delecao ou match diagonal
            float insertion = prev[j];       // celula de cima (linha anterior, mesma coluna)
            float deletion  = curr[j - 1];   // celula da esquerda (mesma linha, coluna anterior)
            float match     = prev[j - 1];   // celula diagonal (linha anterior, coluna anterior)

            curr[j] = cost + std::min({insertion, deletion, match});
        }

        // Troca as linhas: atual vira anterior para a proxima iteracao
        std::swap(prev, curr);
    }

    // Resultado final esta em prev[M] (apos o ultimo swap)
    // Normaliza pelo comprimento total do caminho
    return prev[M] / static_cast<float>(N + M);
}

/**
 * Match com pesos padrao: 60% sensor A (mao dominante), 40% sensor B.
 */
DTWResult DTW::match(const std::vector<Point3D>& trajectoryA,
                     const std::vector<Point3D>& trajectoryB,
                     const std::vector<std::vector<Point3D>>& gesturesA,
                     const std::vector<std::vector<Point3D>>& gesturesB,
                     const std::vector<float>& thresholds) {
    return match(trajectoryA, trajectoryB, gesturesA, gesturesB, thresholds, 0.6f, 0.4f);
}

/**
 * Compara trajetorias capturadas contra todo o banco de gestos.
 *
 * Para cada gesto no banco:
 * 1. Calcula DTW score do sensor A (mao dominante)
 * 2. Calcula DTW score do sensor B (mao auxiliar)
 * 3. Combina com peso: combined = scoreA*weightA + scoreB*weightB
 * 4. Para gestos solo (mao unica): usa 100% sensor A
 *
 * FIX C02: Adicionada validacao de tamanho entre gesturesA, gesturesB
 * e thresholds. Antes, vetores de tamanhos diferentes causavam
 * acesso out-of-bounds em thresholds[bestIndex] → crash.
 *
 * FIX C03: Gestos solo (trajectoryB vazia) agora usam score = scoreA
 * puro ao inves de scoreA*0.6 + FLT_MAX*0.4. Antes, gestos de mao
 * unica NUNCA faziam match porque o score combinado era sempre FLT_MAX.
 */
DTWResult DTW::match(const std::vector<Point3D>& trajectoryA,
                     const std::vector<Point3D>& trajectoryB,
                     const std::vector<std::vector<Point3D>>& gesturesA,
                     const std::vector<std::vector<Point3D>>& gesturesB,
                     const std::vector<float>& thresholds,
                     float weightA, float weightB) {
    DTWResult result;
    result.score = FLT_MAX;
    result.matched = false;
    result.matchIndex = -1;
    result.confidence = 0.0f;
    result.ambiguous = false;  // FIX M06: inicializa flag de ambiguidade

    lastScores.clear();
    lastBestScore = FLT_MAX;

    int gestureCount = static_cast<int>(gesturesA.size());

    // FIX C02: Validacao de consistencia entre vetores
    // Todos devem ter o mesmo tamanho — se nao tem, e bug do caller
    if (gestureCount == 0 ||
        gesturesB.size() != static_cast<size_t>(gestureCount) ||
        thresholds.size() != static_cast<size_t>(gestureCount)) {
        return result;
    }

    // Compara trajetoria capturada contra cada gesto do banco
    for (int i = 0; i < gestureCount; i++) {
        // Score do sensor A (mao dominante) — sempre calculado
        float scoreA = calculate(trajectoryA, gesturesA[i]);

        float combined;

        // FIX C03: Gesto solo = trajectoryB de referencia vazia
        // Usa 100% do score do sensor A, ignorando sensor B
        if (gesturesB[i].empty()) {
            combined = scoreA;
        } else {
            // Gesto dual: combina scores com peso
            // 60% sensor A (mao dominante) + 40% sensor B (mao auxiliar)
            float scoreB = calculate(trajectoryB, gesturesB[i]);
            combined = scoreA * weightA + scoreB * weightB;
        }

        lastScores.push_back(std::make_pair(i, combined));
    }

    // Ordena por score ascendente (melhor match primeiro)
    std::sort(lastScores.begin(), lastScores.end(),
              [](const std::pair<int, float>& a, const std::pair<int, float>& b) {
                  return a.second < b.second;
              });

    // Identifica o melhor match
    if (!lastScores.empty()) {
        int bestIndex = lastScores[0].first;
        float bestScore = lastScores[0].second;

        lastBestScore = bestScore;
        result.score = bestScore;
        result.matchIndex = bestIndex;

        // Verifica se score esta abaixo do threshold do gesto
        // Threshold e individual por gesto (emergencia = 4.5, geral = 3.5)
        if (bestScore < thresholds[bestIndex]) {
            result.matched = true;
            // Confianca: 1.0 = perfeito, 0.0 = no limite do threshold
            result.confidence = 1.0f - (bestScore / thresholds[bestIndex]);
        }

        // FIX M06: Calcula ambiguidade diretamente no resultado
        // Antes, hasAmbiguity() usava lastScores que podia ser de um match anterior
        result.ambiguous = hasAmbiguity();
    }

    return result;
}

/**
 * Verifica se o ultimo match foi ambiguo.
 *
 * Ambiguidade: dois gestos com scores muito proximos — o sistema
 * nao tem certeza de qual o usuario quis fazer. Se a diferenca
 * entre o melhor e o segundo melhor score e < margin, e ambiguo.
 *
 * Margem padrao: 0.5 (definida no header)
 *
 * NOTA: opera sobre o resultado do ultimo match(). Se chamado
 * sem match() previo, retorna false (lastScores vazio).
 */
bool DTW::hasAmbiguity(float margin) {
    if (lastScores.size() < 2) {
        return false;
    }

    float diff = lastScores[1].second - lastScores[0].second;
    return diff < margin;
}

/**
 * Retorna os N melhores matches do ultimo match(), ordenados
 * por score ascendente (melhor primeiro). Util para debug
 * e para mostrar alternativas ao usuario.
 */
std::vector<std::pair<int, float>> DTW::getTopMatches(int n) {
    int count = std::min(n, static_cast<int>(lastScores.size()));
    return std::vector<std::pair<int, float>>(lastScores.begin(),
                                              lastScores.begin() + count);
}
