/**
 * GESTUUM — Orbital Feature Extractor Implementation
 * Responsibility: Extrair features orbitais de amostras brutas do IMU.
 *
 * Pipeline de extracao:
 *   1. Subtrair gravidade (media das amostras pre-onset)
 *   2. Calcular amplitude e pico (distancia ao repouso)
 *   3. PCA 3x3 para linearidade e plano principal
 *   4. Jerk para suavidade
 *   5. Energia gyro para rotacao
 *   6. Correlacao para simetria
 *   7. Duracao do stroke
 */

#include "orbital_extractor.h"
#include <cmath>
#include <cfloat>

// Epsilon para evitar divisao por zero e instabilidade numerica
static constexpr float EPS = 1e-6f;
// PI explicito — M_PI e POSIX, nao ISO C++
static constexpr float PI_F = 3.14159265358979323846f;

// === Pesos para distancia ponderada ===
// Calibrados para maximizar discriminacao entre gestos distintos.
// Linearidade e plano sao os mais discriminativos (forma do movimento).
static constexpr float W_AMPLITUDE   = 1.5f;
static constexpr float W_PEAK        = 1.0f;
static constexpr float W_LINEARITY   = 2.0f;
// Peso do plano reduzido: orientacao do sensor na mao varia entre execucoes,
// causando planos diferentes para o mesmo gesto. 0.7 em vez de 1.5.
static constexpr float W_PLANE       = 0.7f;
static constexpr float W_DURATION    = 0.5f;
static constexpr float W_SMOOTHNESS  = 1.0f;
static constexpr float W_ROTATION    = 1.5f;
static constexpr float W_SYMMETRY    = 1.0f;

// === Ranges esperados para normalizacao ===
// Cada feature e dividida pelo seu range tipico antes da ponderacao.
static constexpr float RANGE_AMPLITUDE  = 2.0f;   // 0 a ~2g
static constexpr float RANGE_PEAK       = 4.0f;   // 0 a ~4g
static constexpr float RANGE_LINEARITY  = 1.0f;   // 0 a 1
static constexpr float RANGE_DURATION   = 3000.0f; // 0 a 3000ms
static constexpr float RANGE_SMOOTHNESS = 1.0f;    // 0 a 1
static constexpr float RANGE_ROTATION   = 1.0f;    // 0 a 1
static constexpr float RANGE_SYMMETRY   = 2.0f;    // -1 a 1

// ============================================================
// extract() — Extrai assinatura orbital de amostras brutas
// ============================================================
OrbitalSignature OrbitalExtractor::extract(
    const RawSample3D* samples, size_t count,
    size_t onsetIdx, size_t offsetIdx,
    const RawSample3D* gyroSamples, size_t gyroCount)
{
    OrbitalSignature sig;

    // Validacao basica: minimo 3 amostras de stroke.
    // Antes era 5, mas gestos curtos (aceno negativo, recusa) geravam
    // strokes de 3-4 amostras e a assinatura ficava invalida.
    if (!samples || count == 0 || offsetIdx <= onsetIdx ||
        (offsetIdx - onsetIdx) < 3) {
        return sig; // valid = false
    }

    size_t strokeLen = offsetIdx - onsetIdx;

    // --- 1. Subtrair gravidade (media das amostras em repouso) ---
    // FIX BUG-03: Se onset e muito cedo (< 5 amostras antes), usar amostras
    // APOS offset como referencia de repouso. Durante reconhecimento de objetos,
    // a gravacao pode comecar com o usuario ja em movimento, tornando as
    // primeiras amostras inuteis para calibracao de gravidade.
    // Apos o offset, o usuario parou (isStable disparou), entao essas amostras
    // sao repouso confiavel.
    float restX = 0.0f, restY = 0.0f, restZ = 0.0f;
    size_t restCount = 0;

    if (onsetIdx >= 5) {
        // Caso normal: amostras antes do onset sao repouso
        restCount = (onsetIdx > 10) ? 10 : onsetIdx;
        for (size_t i = 0; i < restCount; i++) {
            restX += samples[i].x;
            restY += samples[i].y;
            restZ += samples[i].z;
        }
    } else if (offsetIdx < count && (count - offsetIdx) >= 3) {
        // Onset muito cedo: usar amostras APOS offset (cauda = repouso)
        size_t tailSamples = count - offsetIdx;
        restCount = (tailSamples > 10) ? 10 : tailSamples;
        for (size_t i = 0; i < restCount; i++) {
            restX += samples[offsetIdx + i].x;
            restY += samples[offsetIdx + i].y;
            restZ += samples[offsetIdx + i].z;
        }
    } else {
        // Ultimo recurso: usar a primeira amostra disponivel
        restCount = 1;
        restX = samples[0].x;
        restY = samples[0].y;
        restZ = samples[0].z;
    }

    restX /= restCount;
    restY /= restCount;
    restZ /= restCount;

    // --- 2. Amplitude e pico ---
    float sumMag = 0.0f;
    float maxMag = 0.0f;

    for (size_t i = onsetIdx; i < offsetIdx; i++) {
        float dx = samples[i].x - restX;
        float dy = samples[i].y - restY;
        float dz = samples[i].z - restZ;
        float mag = sqrtf(dx * dx + dy * dy + dz * dz);
        sumMag += mag;
        if (mag > maxMag) maxMag = mag;
    }

    sig.amplitude = sumMag / strokeLen;
    sig.peak = maxMag;

    // --- 3. PCA 3x3 para linearidade e plano principal ---
    float mean[3], cov[3][3];
    computeCovariance(samples, onsetIdx, offsetIdx, mean, cov);

    float eigenvalues[3];
    float eigenvectors[3][3];
    eigendecomposition3x3(cov, eigenvalues, eigenvectors);

    // Linearidade: se lambda[0] >> lambda[1], movimento e linear (excentricidade alta)
    // Se lambda[0] ≈ lambda[1], movimento e planar/circular
    sig.linearity = (eigenvalues[0] - eigenvalues[1]) / (eigenvalues[0] + EPS);
    if (sig.linearity < 0.0f) sig.linearity = 0.0f;
    if (sig.linearity > 1.0f) sig.linearity = 1.0f;

    // Plano normal: eigenvector do MENOR eigenvalue (direcao de menor variancia)
    // Convencao: componente Z positiva para consistencia de sinal
    sig.planeNormal[0] = eigenvectors[0][2];
    sig.planeNormal[1] = eigenvectors[1][2];
    sig.planeNormal[2] = eigenvectors[2][2];
    if (sig.planeNormal[2] < 0.0f) {
        sig.planeNormal[0] = -sig.planeNormal[0];
        sig.planeNormal[1] = -sig.planeNormal[1];
        sig.planeNormal[2] = -sig.planeNormal[2];
    }

    // --- 4. Suavidade (inverso do jerk medio) ---
    // Jerk = derivada da aceleracao = diff(accel)/dt
    float sumJerk = 0.0f;
    size_t jerkCount = 0;
    for (size_t i = onsetIdx + 1; i < offsetIdx; i++) {
        float jx = samples[i].x - samples[i - 1].x;
        float jy = samples[i].y - samples[i - 1].y;
        float jz = samples[i].z - samples[i - 1].z;
        sumJerk += sqrtf(jx * jx + jy * jy + jz * jz);
        jerkCount++;
    }
    float meanJerk = (jerkCount > 0) ? (sumJerk / jerkCount) : 0.0f;
    // Normalizado para [0, 1]: 1.0 = muito suave, 0.0 = muito brusco
    sig.smoothness = 1.0f / (1.0f + meanJerk * 10.0f);

    // --- 5. Rotacao (energia gyro / energia total) ---
    if (gyroSamples && gyroCount > 0) {
        float gyroEnergy = 0.0f;
        float accelEnergy = 0.0f;

        // Usar indice proporcional para alinhar gyro com accel
        size_t gyroOnset = (onsetIdx * gyroCount) / count;
        size_t gyroOffset = (offsetIdx * gyroCount) / count;
        if (gyroOffset > gyroCount) gyroOffset = gyroCount;
        if (gyroOffset <= gyroOnset) gyroOffset = gyroOnset + 1;

        for (size_t i = gyroOnset; i < gyroOffset && i < gyroCount; i++) {
            gyroEnergy += gyroSamples[i].x * gyroSamples[i].x +
                          gyroSamples[i].y * gyroSamples[i].y +
                          gyroSamples[i].z * gyroSamples[i].z;
        }

        for (size_t i = onsetIdx; i < offsetIdx; i++) {
            float dx = samples[i].x - restX;
            float dy = samples[i].y - restY;
            float dz = samples[i].z - restZ;
            accelEnergy += dx * dx + dy * dy + dz * dz;
        }

        float totalEnergy = gyroEnergy + accelEnergy + EPS;
        sig.rotation = gyroEnergy / totalEnergy;
    } else {
        sig.rotation = 0.0f;
    }

    // --- 6. Simetria (correlacao entre 1a e 2a metade invertida) ---
    size_t halfLen = strokeLen / 2;
    if (halfLen >= 3) {
        // Calcular magnitude de cada amostra do stroke (ja sem gravidade)
        float sumA = 0.0f, sumB = 0.0f;
        float sumA2 = 0.0f, sumB2 = 0.0f, sumAB = 0.0f;

        for (size_t i = 0; i < halfLen; i++) {
            size_t idxA = onsetIdx + i;
            // 2a metade invertida: ultimo elemento primeiro
            size_t idxB = offsetIdx - 1 - i;

            float dxA = samples[idxA].x - restX;
            float dyA = samples[idxA].y - restY;
            float dzA = samples[idxA].z - restZ;
            float magA = sqrtf(dxA * dxA + dyA * dyA + dzA * dzA);

            float dxB = samples[idxB].x - restX;
            float dyB = samples[idxB].y - restY;
            float dzB = samples[idxB].z - restZ;
            float magB = sqrtf(dxB * dxB + dyB * dyB + dzB * dzB);

            sumA += magA;
            sumB += magB;
            sumA2 += magA * magA;
            sumB2 += magB * magB;
            sumAB += magA * magB;
        }

        // Correlacao de Pearson
        float n = (float)halfLen;
        float num = n * sumAB - sumA * sumB;
        float den = sqrtf((n * sumA2 - sumA * sumA) * (n * sumB2 - sumB * sumB));
        sig.symmetry = (den > EPS) ? (num / den) : 0.0f;
    } else {
        sig.symmetry = 0.0f;
    }

    // --- 7. Duracao do stroke em ms ---
    // A 50Hz, cada amostra = 20ms
    sig.duration = (float)(strokeLen * IMU_SAMPLE_PERIOD_MS);

    sig.valid = true;
    return sig;
}

// ============================================================
// distance() — Distancia ponderada entre duas assinaturas
// ============================================================
float OrbitalExtractor::distance(const OrbitalSignature& a, const OrbitalSignature& b) {
    if (!a.valid || !b.valid) return FLT_MAX;

    float sum = 0.0f;

    // Amplitude (normalizada pelo range esperado)
    float dAmp = (a.amplitude - b.amplitude) / RANGE_AMPLITUDE;
    sum += W_AMPLITUDE * dAmp * dAmp;

    // Pico
    float dPeak = (a.peak - b.peak) / RANGE_PEAK;
    sum += W_PEAK * dPeak * dPeak;

    // Linearidade (ja esta em [0, 1])
    float dLin = (a.linearity - b.linearity) / RANGE_LINEARITY;
    sum += W_LINEARITY * dLin * dLin;

    // Plano normal — distancia angular (0 = paralelos, 1 = perpendiculares)
    // Usa |dot product| porque o sinal da normal e arbitrario
    float dot = a.planeNormal[0] * b.planeNormal[0] +
                a.planeNormal[1] * b.planeNormal[1] +
                a.planeNormal[2] * b.planeNormal[2];
    float dPlane = 1.0f - fabsf(dot); // 0 = mesmo plano, 1 = perpendicular
    sum += W_PLANE * dPlane * dPlane;

    // Duracao
    float dDur = (a.duration - b.duration) / RANGE_DURATION;
    sum += W_DURATION * dDur * dDur;

    // Suavidade
    float dSmooth = (a.smoothness - b.smoothness) / RANGE_SMOOTHNESS;
    sum += W_SMOOTHNESS * dSmooth * dSmooth;

    // Rotacao
    float dRot = (a.rotation - b.rotation) / RANGE_ROTATION;
    sum += W_ROTATION * dRot * dRot;

    // Simetria
    float dSym = (a.symmetry - b.symmetry) / RANGE_SYMMETRY;
    sum += W_SYMMETRY * dSym * dSym;

    return sqrtf(sum);
}

// ============================================================
// distanceToScore() — Converte distancia em score [0, 1]
// ============================================================
float OrbitalExtractor::distanceToScore(float dist) {
    // Score = 1 / (1 + dist). Dist=0 → score=1. Dist→∞ → score→0.
    if (dist < 0.0f || dist >= FLT_MAX) return 0.0f;
    return 1.0f / (1.0f + dist);
}

// ============================================================
// average() — Media element-wise de N assinaturas
// ============================================================
OrbitalSignature OrbitalExtractor::average(const OrbitalSignature* sigs, size_t count) {
    OrbitalSignature avg;
    if (!sigs || count == 0) return avg;

    // Contar apenas assinaturas validas
    size_t validCount = 0;
    for (size_t i = 0; i < count; i++) {
        if (!sigs[i].valid) continue;
        avg.amplitude += sigs[i].amplitude;
        avg.peak += sigs[i].peak;
        avg.linearity += sigs[i].linearity;
        avg.planeNormal[0] += sigs[i].planeNormal[0];
        avg.planeNormal[1] += sigs[i].planeNormal[1];
        avg.planeNormal[2] += sigs[i].planeNormal[2];
        avg.duration += sigs[i].duration;
        avg.smoothness += sigs[i].smoothness;
        avg.rotation += sigs[i].rotation;
        avg.symmetry += sigs[i].symmetry;
        validCount++;
    }

    if (validCount == 0) return avg;

    float n = (float)validCount;
    avg.amplitude /= n;
    avg.peak /= n;
    avg.linearity /= n;
    avg.planeNormal[0] /= n;
    avg.planeNormal[1] /= n;
    avg.planeNormal[2] /= n;
    avg.duration /= n;
    avg.smoothness /= n;
    avg.rotation /= n;
    avg.symmetry /= n;

    // Normalizar vetor normal do plano (pode ter perdido unitariedade na media)
    float normLen = sqrtf(
        avg.planeNormal[0] * avg.planeNormal[0] +
        avg.planeNormal[1] * avg.planeNormal[1] +
        avg.planeNormal[2] * avg.planeNormal[2]
    );
    if (normLen > EPS) {
        avg.planeNormal[0] /= normLen;
        avg.planeNormal[1] /= normLen;
        avg.planeNormal[2] /= normLen;
    }

    avg.valid = true;
    return avg;
}

// ============================================================
// maxPairDistance() — Maior distancia entre qualquer par
// ============================================================
float OrbitalExtractor::maxPairDistance(const OrbitalSignature* sigs, size_t count) {
    float maxDist = 0.0f;
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (!sigs[i].valid || !sigs[j].valid) continue;
            float d = distance(sigs[i], sigs[j]);
            if (d > maxDist) maxDist = d;
        }
    }
    return maxDist;
}

// ============================================================
// computeCovariance() — Matriz de covariancia 3x3
// ============================================================
void OrbitalExtractor::computeCovariance(
    const RawSample3D* samples,
    size_t onset, size_t offset,
    float mean[3], float cov[3][3])
{
    size_t n = offset - onset;

    // Calcular media (centroide)
    mean[0] = mean[1] = mean[2] = 0.0f;
    for (size_t i = onset; i < offset; i++) {
        mean[0] += samples[i].x;
        mean[1] += samples[i].y;
        mean[2] += samples[i].z;
    }
    mean[0] /= n;
    mean[1] /= n;
    mean[2] /= n;

    // Calcular covariancia
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            cov[r][c] = 0.0f;

    for (size_t i = onset; i < offset; i++) {
        float d[3] = {
            samples[i].x - mean[0],
            samples[i].y - mean[1],
            samples[i].z - mean[2]
        };
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                cov[r][c] += d[r] * d[c];
    }

    // Normalizar pela quantidade de amostras
    float invN = 1.0f / (float)n;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            cov[r][c] *= invN;
}

// ============================================================
// eigendecomposition3x3() — Solucao analitica (Cardano)
// ============================================================
// Resolve eigenvalues de uma matriz simetrica real 3x3 usando
// a equacao cubica caracteristica. Sem iteracao, O(1).
// Referencia: Smith, O.K. (1961) "Eigenvalues of a symmetric 3x3 matrix."
// Adaptado para single-precision float com regularizacao epsilon.
void OrbitalExtractor::eigendecomposition3x3(
    const float cov[3][3],
    float eigenvalues[3],
    float eigenvectors[3][3])
{
    // Traco e invariantes da matriz simetrica
    float a00 = cov[0][0], a01 = cov[0][1], a02 = cov[0][2];
    float a11 = cov[1][1], a12 = cov[1][2];
    float a22 = cov[2][2];

    // p1 = soma dos quadrados dos elementos fora da diagonal
    float p1 = a01 * a01 + a02 * a02 + a12 * a12;

    if (p1 < EPS) {
        // Matriz ja e diagonal — eigenvalues sao os elementos da diagonal
        eigenvalues[0] = a00;
        eigenvalues[1] = a11;
        eigenvalues[2] = a22;

        // Ordenar decrescente com eigenvectors correspondentes
        // Identidade como eigenvectors (eixos alinhados)
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                eigenvectors[r][c] = (r == c) ? 1.0f : 0.0f;

        // Bubble sort simples para 3 elementos
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2 - i; j++) {
                if (eigenvalues[j] < eigenvalues[j + 1]) {
                    // Trocar eigenvalues
                    float tmp = eigenvalues[j];
                    eigenvalues[j] = eigenvalues[j + 1];
                    eigenvalues[j + 1] = tmp;
                    // Trocar colunas de eigenvectors
                    for (int r = 0; r < 3; r++) {
                        float t = eigenvectors[r][j];
                        eigenvectors[r][j] = eigenvectors[r][j + 1];
                        eigenvectors[r][j + 1] = t;
                    }
                }
            }
        }
        return;
    }

    // Traco da matriz
    float trace = a00 + a11 + a22;
    float q = trace / 3.0f;

    // Matriz centrada: B = A - qI
    float b00 = a00 - q, b11 = a11 - q, b22 = a22 - q;

    // p = sqrt(sum of squares of B / 6)
    float p2 = (b00 * b00 + b11 * b11 + b22 * b22 + 2.0f * p1) / 6.0f;
    float p = sqrtf(p2 + EPS);

    // B = (1/p) * (A - qI)
    float invP = 1.0f / (p + EPS);
    float B[3][3] = {
        { b00 * invP, a01 * invP, a02 * invP },
        { a01 * invP, b11 * invP, a12 * invP },
        { a02 * invP, a12 * invP, b22 * invP }
    };

    // Determinante de B (formula direta para 3x3)
    float detB = B[0][0] * (B[1][1] * B[2][2] - B[1][2] * B[2][1])
               - B[0][1] * (B[1][0] * B[2][2] - B[1][2] * B[2][0])
               + B[0][2] * (B[1][0] * B[2][1] - B[1][1] * B[2][0]);

    // r = det(B) / 2, clamped para [-1, 1]
    float r = detB / 2.0f;
    if (r < -1.0f) r = -1.0f;
    if (r > 1.0f) r = 1.0f;

    // phi = acos(r) / 3
    float phi = acosf(r) / 3.0f;

    // Eigenvalues (em ordem decrescente)
    eigenvalues[0] = q + 2.0f * p * cosf(phi);
    eigenvalues[2] = q + 2.0f * p * cosf(phi + 2.0f * PI_F / 3.0f);
    eigenvalues[1] = trace - eigenvalues[0] - eigenvalues[2]; // Garante soma = trace

    // Garantir ordem decrescente
    if (eigenvalues[1] > eigenvalues[0]) {
        float tmp = eigenvalues[0];
        eigenvalues[0] = eigenvalues[1];
        eigenvalues[1] = tmp;
    }
    if (eigenvalues[2] > eigenvalues[1]) {
        float tmp = eigenvalues[1];
        eigenvalues[1] = eigenvalues[2];
        eigenvalues[2] = tmp;
    }
    if (eigenvalues[1] > eigenvalues[0]) {
        float tmp = eigenvalues[0];
        eigenvalues[0] = eigenvalues[1];
        eigenvalues[1] = tmp;
    }

    // Regularizar eigenvalues negativos (arredondamento numerico)
    for (int i = 0; i < 3; i++) {
        if (eigenvalues[i] < 0.0f) eigenvalues[i] = 0.0f;
    }

    // === Calcular eigenvectors por (A - lambda*I) null space ===
    // Para cada eigenvalue, resolver (A - lambda*I) * v = 0
    // usando produto cruzado de duas linhas da matriz (A - lambda*I)
    for (int k = 0; k < 3; k++) {
        float M[3][3] = {
            { a00 - eigenvalues[k], a01,                    a02                    },
            { a01,                   a11 - eigenvalues[k],  a12                    },
            { a02,                   a12,                    a22 - eigenvalues[k]   }
        };

        // Produto cruzado de linhas 0 e 1
        float v[3];
        v[0] = M[0][1] * M[1][2] - M[0][2] * M[1][1];
        v[1] = M[0][2] * M[1][0] - M[0][0] * M[1][2];
        v[2] = M[0][0] * M[1][1] - M[0][1] * M[1][0];

        float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

        if (len < EPS) {
            // Tentar linhas 0 e 2
            v[0] = M[0][1] * M[2][2] - M[0][2] * M[2][1];
            v[1] = M[0][2] * M[2][0] - M[0][0] * M[2][2];
            v[2] = M[0][0] * M[2][1] - M[0][1] * M[2][0];
            len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        }

        if (len < EPS) {
            // Tentar linhas 1 e 2
            v[0] = M[1][1] * M[2][2] - M[1][2] * M[2][1];
            v[1] = M[1][2] * M[2][0] - M[1][0] * M[2][2];
            v[2] = M[1][0] * M[2][1] - M[1][1] * M[2][0];
            len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        }

        if (len < EPS) {
            // Fallback: eixo canonico
            v[0] = (k == 0) ? 1.0f : 0.0f;
            v[1] = (k == 1) ? 1.0f : 0.0f;
            v[2] = (k == 2) ? 1.0f : 0.0f;
            len = 1.0f;
        }

        // Normalizar
        eigenvectors[0][k] = v[0] / len;
        eigenvectors[1][k] = v[1] / len;
        eigenvectors[2][k] = v[2] / len;
    }
}
