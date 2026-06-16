/**
 * GESTUUM — Orbital Feature Extractor
 * Responsibility: Extrair features orbitais (assinatura) de amostras brutas do IMU.
 *
 * O modelo orbital trata cada gesto como uma trajetoria no espaco de aceleracao,
 * similar a um cometa orbitando um sol (ponto de repouso). Em vez de comparar
 * sequencias de celulas ponto-a-ponto, extrai 8 features continuas que descrevem
 * a FORMA do gesto: amplitude, linearidade, plano, suavidade, rotacao, simetria.
 *
 * Fundamentado no Laban Movement Analysis (fatores de Effort) e mecanica orbital.
 * PCA 3x3 resolvida por formula de Cardano (O(1), sem iteracao, sem libs externas).
 */

#ifndef GESTUUM_ORBITAL_EXTRACTOR_H
#define GESTUUM_ORBITAL_EXTRACTOR_H

#include "matrix3d.h"      // RawSample3D
#include "gesture_data.h"  // OrbitalSignature

class OrbitalExtractor {
public:
    /**
     * Extrai assinatura orbital a partir de amostras brutas do IMU.
     * @param samples    Buffer de amostras brutas (acelerometro ou giroscopio)
     * @param count      Numero total de amostras no buffer
     * @param onsetIdx   Indice do inicio do stroke no buffer
     * @param offsetIdx  Indice do fim do stroke no buffer
     * @param gyroSamples Buffer de amostras do giroscopio (nullptr se nao disponivel)
     * @param gyroCount  Numero de amostras do giroscopio
     * @return OrbitalSignature com features extraidas (valid=true se sucesso)
     */
    static OrbitalSignature extract(
        const RawSample3D* samples, size_t count,
        size_t onsetIdx, size_t offsetIdx,
        const RawSample3D* gyroSamples = nullptr, size_t gyroCount = 0
    );

    /**
     * Calcula distancia ponderada entre duas assinaturas orbitais.
     * Retorna valor >= 0. Menor = mais similar.
     * @param a Primeira assinatura
     * @param b Segunda assinatura
     * @return Distancia ponderada (0.0 = identicas)
     */
    static float distance(const OrbitalSignature& a, const OrbitalSignature& b);

    /**
     * Converte distancia orbital em score de similaridade [0, 1].
     * @param dist Distancia retornada por distance()
     * @return Score de 0.0 (completamente diferente) a 1.0 (identico)
     */
    static float distanceToScore(float dist);

    /**
     * Calcula media element-wise de N assinaturas (para treino).
     * @param sigs Array de assinaturas
     * @param count Numero de assinaturas (tipicamente 3)
     * @return Assinatura media
     */
    static OrbitalSignature average(const OrbitalSignature* sigs, size_t count);

    /**
     * Calcula maior distancia entre qualquer par de assinaturas (para threshold).
     * @param sigs Array de assinaturas
     * @param count Numero de assinaturas
     * @return Maior distancia entre qualquer par
     */
    static float maxPairDistance(const OrbitalSignature* sigs, size_t count);

private:
    /**
     * Eigendecomposition analitica de matriz simetrica 3x3 (Cardano).
     * O(1), sem iteracao, sem dependencias externas.
     * Eigenvalues retornados em ordem decrescente: lambda[0] >= lambda[1] >= lambda[2].
     * Eigenvectors nas colunas correspondentes de vecs.
     */
    static void eigendecomposition3x3(
        const float cov[3][3],
        float eigenvalues[3],
        float eigenvectors[3][3]
    );

    /**
     * Calcula matriz de covariancia 3x3 a partir de amostras do stroke.
     * Tambem retorna a media (centroide) das amostras.
     */
    static void computeCovariance(
        const RawSample3D* samples,
        size_t onset, size_t offset,
        float mean[3], float cov[3][3]
    );
};

#endif // GESTUUM_ORBITAL_EXTRACTOR_H
