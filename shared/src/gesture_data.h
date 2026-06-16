/**
 * GESTUUM — Gesture Data Structures
 * Bloco: 1.3 — Biblioteca shared/ - DTW
 * Responsibility: Define structures for gesture definitions and context definitions
 *                 used by the gesture recognition engine.
 */

#ifndef GESTUUM_GESTURE_DATA_H
#define GESTUUM_GESTURE_DATA_H

#include "constants.h"
#include "matrix3d.h"
#include "protocol.h"
#include <vector>
#include <cstring>

/**
 * Assinatura orbital de um gesto — 8 features que descrevem a FORMA do movimento.
 * Baseado no Laban Movement Analysis (Effort factors) e mecanica orbital.
 * Cada gesto treinado tem uma assinatura; o reconhecimento compara assinaturas.
 *
 * amplitude  = distancia media do repouso (semi-eixo maior)
 * peak       = distancia maxima do repouso (velocidade no perielio)
 * linearity  = razao de eigenvalues: 1.0=linear, 0.0=circular (excentricidade)
 * planeNormal= vetor normal ao plano principal do movimento (inclinacao)
 * duration   = duracao do stroke em ms (periodo orbital)
 * smoothness = inverso do jerk medio — quao fluido e o movimento
 * rotation   = energia gyro / energia total — translacao vs rotacao
 * symmetry   = correlacao entre 1a metade e 2a metade invertida
 */
struct OrbitalSignature {
    float amplitude;
    float peak;
    float linearity;
    float planeNormal[3];
    float duration;
    float smoothness;
    float rotation;
    float symmetry;
    bool valid;           // true se extraido com sucesso

    OrbitalSignature()
        : amplitude(0), peak(0), linearity(0)
        , duration(0), smoothness(0), rotation(0), symmetry(0)
        , valid(false) {
        planeNormal[0] = 0.0f;
        planeNormal[1] = 0.0f;
        planeNormal[2] = 1.0f;
    }
};

/**
 * Defines a single gesture in the gesture database.
 *
 * Each gesture has a unique ID, a category, and pre-recorded reference
 * trajectories for sensor A (dominant hand) and sensor B (secondary hand).
 * Solo gestures use only sensor A trajectory.
 */
struct GestureDefinition {
    uint16_t id;                             // Unique gesture ID (e.g., 0x0101 for G01)
    char idStr[8];                           // String ID original do JSON (e.g., "G01", "CX01"). Necessario pro menu local (Caminho C/Sprint C2) usar startTrainingMode(idStr,...) sem reverse de uint16->string.
    char name[32];                           // Human-readable name ("agua", "socorro")
    Category category;                       // Category enum (CAT_GERAL, CAT_EMERGENCIA, etc.)
    char audioFile[32];                      // Audio file name on SPIFFS ("agua.wav")
    float threshold;                         // DTW threshold for this gesture
    bool isSolo;                             // True if single-hand gesture (sensor A only)
    bool isContext;                          // True if this is a context prefix gesture (phrase building)
    AutomationCmd automationCmd;             // LED/automation command (CMD_NONE if none)
    Profile profile;                         // FIX L01: Perfil associado (PROFILE_BASE para categorias base)
    bool trained;                            // FIX INSTALL-17: true se treinado pelo usuario, false = placeholder
    // NOTA M01: std::vector aloca no heap. Com 150 gestos, sao 300 vetores
    // separados que fragmentam a memoria do ESP32 ao longo do tempo.
    // Para MVP e aceitavel. Se houver problemas de memoria apos horas de uso,
    // considerar trocar para array fixo: Point3D trajectoryA[DTW_MAX_TRAJECTORY_LEN]
    // com uint16_t trajectoryALen, eliminando alocacao dinamica.
    std::vector<Point3D> trajectoryA;        // Trajetoria de referencia sensor A (accel)
    std::vector<Point3D> trajectoryB;        // Trajetoria de referencia sensor B (accel)
    std::vector<Point3D> trajectoryAGyro;    // FIX INSTALL-20: Trajetoria gyro sensor A
    std::vector<Point3D> trajectoryBGyro;    // FIX INSTALL-20: Trajetoria gyro sensor B
    // Assinaturas orbitais — features que descrevem a forma do gesto.
    // Extraidas a partir das trajetorias durante treino.
    // Usadas para reconhecimento no lugar da comparacao ponto-a-ponto.
    OrbitalSignature signatureA;        // Assinatura accel sensor A (mao dominante)
    OrbitalSignature signatureB;        // Assinatura accel sensor B (mao secundaria)
    OrbitalSignature signatureAGyro;    // Assinatura gyro sensor A
    uint16_t durationMs;                     // Estimated gesture duration in milliseconds

    // Threshold por gesto (calculado no treino com N repeticoes).
    // Se 0.0, usa ORBITAL_MATCH_THRESHOLD global como fallback.
    float orbitalThreshold;              // Threshold orbital minimo para aceitar match
    float orbitalScoreMean;              // Media dos scores nas repeticoes de treino
    float orbitalScoreStd;               // Desvio padrao dos scores de treino

    /**
     * Constructor with sensible defaults.
     */
    GestureDefinition()
        : id(0)
        , category(CAT_GERAL)
        , threshold(DTW_THRESHOLD_DEFAULT)
        , isSolo(false)
        , isContext(false)
        , automationCmd(CMD_NONE)
        , profile(PROFILE_BASE)
        , trained(false)
        , durationMs(0)
        , orbitalThreshold(0.0f)
        , orbitalScoreMean(0.0f)
        , orbitalScoreStd(0.0f) {
        memset(name, 0, sizeof(name));
        memset(audioFile, 0, sizeof(audioFile));
        memset(idStr, 0, sizeof(idStr));   // Sprint C-audit A07: profilatica
    }
};

/**
 * Defines a context prefix used in sentence construction.
 *
 * Contexts are selected via sensor B gestures and provide the sentence
 * prefix (e.g., "Preciso de", "Quero") before a main gesture noun.
 */
struct ContextDefinition {
    uint8_t id;                              // Context ID (1-20)
    char prefix[48];                         // Text prefix ("Preciso de", "Quero")
    char audioFile[32];                      // Audio file name on SPIFFS
    std::vector<Point3D> trajectory;         // Reference trajectory (sensor B)
    OrbitalSignature signature;              // Assinatura orbital do gesto de contexto

    /**
     * Constructor with zeroed fields.
     */
    ContextDefinition()
        : id(0) {
        memset(prefix, 0, sizeof(prefix));
        memset(audioFile, 0, sizeof(audioFile));
    }
};

#endif // GESTUUM_GESTURE_DATA_H
