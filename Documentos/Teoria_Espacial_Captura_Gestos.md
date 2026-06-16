# Captura de Gesto no Gestuum — Teoria Espacial

> Como o Gestuum transforma o movimento da mão em fala, usando teoria orbital e análise espacial 3D.

---

## 1. Visão Geral: O Gesto como Órbita no Espaço

O Gestuum trata cada gesto como **a trajetória de um cometa orbitando um sol**. O punho do usuário é o cometa — o repouso (gravidade) é o sol. Quando você move a mão, está "saindo da órbita de repouso" e criando um rastro no espaço 3D.

```
        REPOUSO (sol/centro)
              ☀️
             /|\
            / | \
           /  |  \        ← O gesto "puxa" o punho para
          /   |   \          fora do centro de repouso
         /    |    \
        🌀----+----→ ★     ← Trajetória = a "órbita" do gesto
              |
              ↓
```

O sensor (MPU6886 no M5StickC Plus2) captura **aceleração** e **rotação** a **50 amostras por segundo**. Cada amostra é um ponto no espaço 3D.

---

## 2. Pipeline Completo: Do Movimento ao Reconhecimento

```
┌─────────────────────────────────────────────────────────────────────┐
│                    PIPELINE DE CAPTURA DE GESTO                     │
│                                                                     │
│  ① DOUBLE-TAP         ② LEITURA IMU        ③ FILTRO EMA            │
│  ┌──────────┐         ┌──────────┐         ┌──────────┐            │
│  │ Pancada  │────────→│ Accel XYZ│────────→│ Suaviza  │            │
│  │ > 2.2g   │ ATIVA   │ Gyro XYZ │  50Hz   │ ruído    │            │
│  │ no sensor│ GRAVAÇÃO│ a cada   │         │ α = 0.3  │            │
│  └──────────┘         │ 20ms     │         └────┬─────┘            │
│                       └──────────┘              │                   │
│                                                 ▼                   │
│  ⑥ MATCHING           ⑤ EXTRAÇÃO ORBITAL   ④ GRID 3D              │
│  ┌──────────┐         ┌──────────┐         ┌──────────┐            │
│  │ Compara  │◄────────│ 8 features│◄────────│ Mapeia em│            │
│  │ com gestos│         │ da órbita │         │ cubo     │            │
│  │ treinados│         │ do gesto  │         │ 7×7×7    │            │
│  └────┬─────┘         └──────────┘         └──────────┘            │
│       │                                                             │
│       ▼                                                             │
│  ⑦ RESULTADO                                                       │
│  ┌──────────────────┐                                               │
│  │ "água" → 🔊 play │                                              │
│  └──────────────────┘                                               │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 3. O Espaço 3D — O Cubo de Aceleração

O sensor mede aceleração em 3 eixos. Imagine que seu punho está no centro de um cubo:

```
         Y (cima/baixo)
         ↑
         │    ╔═══════════════════╗
         │   ╱                   ╱│
         │  ╱   CUBO 7×7×7     ╱ │
         │ ╱   (343 células)  ╱  │
         │╔═══════════════════╗   │
         │║                   ║   │
         │║    [3,3,3]        ║   │    Z (frente/trás)
         │║      ●            ║   │   ╱
         │║   REPOUSO         ║   ╝  ╱
         │║   (centro)        ║  ╱  ╱
         │║                   ║ ╱  ╱
         │╚═══════════════════╝╱  ╱
         │                     ╱
         └────────────────────────→ X (esquerda/direita)

    Cada célula ≈ 0.57g de aceleração
    Range total: ±2.0g por eixo
```

**O que acontece:**

| Movimento do punho  | Célula no grid       |
|---------------------|----------------------|
| Repouso             | `[3,3,3]` (centro)  |
| Move para a direita | `[5,3,3]` ou `[6,3,3]` |
| Levanta a mão       | `[3,5,3]` ou `[3,6,3]` |
| Empurra pra frente  | `[3,3,5]` ou `[3,3,6]` |

O **caminho** entre as células é a **trajetória do gesto** — a sequência de células visitadas forma o "desenho" do movimento no espaço.

### Por que 7×7×7 e não 11×11×11?

| Grid        | Tamanho da célula | Comportamento                    |
|-------------|-------------------|----------------------------------|
| 11×11×11    | 0.36g             | Tremor natural da mão (~0.5g) causa mudanças falsas |
| **7×7×7**   | **0.57g**         | **Absorve tremor, preserva a forma do gesto** |

A grade de 7×7×7 foi calibrada para o hardware real — grande o suficiente para ignorar tremor, pequena o suficiente para capturar gestos distintos.

---

## 4. Detecção de Stroke — Quando o Gesto Começa e Termina

O sistema precisa saber exatamente **onde o gesto começa** (onset) e **onde termina** (offset). Isso é feito por uma máquina de estados:

```
    Aceleração
    (magnitude)
        ↑
   1.5g │                    ╱\
        │                   ╱  \
   1.0g │                  ╱    \        ╱\
        │                 ╱      \      ╱  \
   0.5g │    ────────────╱────────\────╱────\──────────────
        │   ↑ REPOUSO   ↑         \  ╱      \  ↑
   0.3g │ ···············│··ONSET···\╱········│·OFFSET·····
        │   (gravidade)  │ threshold         │ (0.15g)
   0.0g │               │                   │
        └──────────────|─┼────────────────|──┼──────────→ tempo
                       │ │                │  │
                       │ ├── INÍCIO ──────┤  │
                       │ │  do stroke     │  │
                       │ │                │  │
                       │ │  3 amostras    │ 25 amostras │
                       │ │  acima 0.3g    │ abaixo 0.15g│
                       │ │  = CONFIRMADO  │ = PAROU     │
                       │ │                │              │
                  cooldown               gesto          estável
                  (300ms)              capturado
```

### Máquina de Estados do Stroke

```
    ┌────────┐   desvio > 0.3g    ┌───────────────┐   desvio < 0.15g    ┌────────────────┐
    │        │   por 3 amostras   │               │   por 25 amostras   │                │
    │  REST  │ ──────────────────→│ STROKE_ACTIVE │ ──────────────────→ │ STROKE_COMPLETE│
    │        │                    │               │                     │                │
    └────────┘                    └───────────────┘                     └────────────────┘
       ↑ cooldown 300ms                                                        │
       └───────────────────────────────────────────────────────────────────────┘
```

**Por que dois thresholds diferentes?**

| Threshold | Valor | Motivo |
|-----------|-------|--------|
| **Onset** (início) | 0.3g | Alto para não disparar com tremor natural da mão |
| **Offset** (fim)   | 0.15g | Baixo para capturar o gesto completo, incluindo a desaceleração final |

### Constantes Críticas do Stroke

| Parâmetro | Valor | Significado |
|-----------|-------|-------------|
| `ORBITAL_ONSET_THRESHOLD_G`  | 0.3g | Desvio mínimo para iniciar captura |
| `ORBITAL_OFFSET_THRESHOLD_G` | 0.15g | Desvio máximo para considerar parado |
| `ORBITAL_ONSET_SAMPLES`      | 3 | Amostras consecutivas para confirmar início (60ms) |
| `ORBITAL_OFFSET_SAMPLES`     | 25 | Amostras consecutivas para confirmar fim (500ms) |
| `ORBITAL_MIN_STROKE_SAMPLES` | 50 | Mínimo de amostras para stroke válido (1s) |
| `STROKE_COOLDOWN_SAMPLES`    | 15 | Amostras ignoradas após double-tap (300ms) |
| `REST_CALIBRATION_SAMPLES`   | 5 | Amostras para calibrar magnitude de repouso |

---

## 5. Filtro EMA — Suavização do Ruído

Antes de processar, os dados brutos passam por um **Filtro de Média Móvel Exponencial** (EMA):

```
    Sinal Bruto (com ruído)              Sinal Filtrado (suave)

    ↑                                    ↑
    │ ╱\  ╱╲                             │
    │╱  ╲╱  ╲  ╱╲                        │    ╱──╲
    │        ╲╱  ╲ ╱╲                    │   ╱    ╲
    │             ╲╱  ╲                  │  ╱      ╲──╲
    │                  ╲                 │ ╱            ╲
    └────────────────────→               └────────────────→
         muito ruidoso                       movimento real
```

**Fórmula:**

```
filtrado = 0.3 × amostra_atual + 0.7 × filtrado_anterior
                ↑                       ↑
            peso novo (30%)        peso histórico (70%)
```

- **α = 0.3** — agressivo, prioriza suavidade
- Remove vibrações de alta frequência (tremor do sensor, ruído elétrico)
- Mantém o movimento real do gesto (frequências baixas)

---

## 6. Integração de Posição — Do Acelerômetro ao Grid

O acelerômetro mede **aceleração**, mas o grid precisa de **posição**. A conversão usa integração com amortecimento:

```
    ACELERAÇÃO (sensor)  →  POSIÇÃO (grid)

    posição = posição_anterior × 0.95 + aceleração_filtrada × 0.05
                                  ↑                              ↑
                          amortecimento                    contribuição
                          (puxa de volta                   do movimento
                           ao centro)                      atual

    Sem amortecimento:                Com amortecimento (0.95):
    posição diverge →                 posição volta ao centro →
    │        ╱                        │      ╱╲
    │      ╱                          │    ╱    ╲
    │    ╱                            │  ╱        ╲──
    │  ╱                              │╱
    │╱                                │
    └──────→ tempo                    └──────→ tempo
    (drift infinito)                  (estável)
```

Depois da integração, a posição contínua é **mapeada para o grid discreto**:

```
    normalizado = (posição / 2.0g) × 3          ← escala para [-3, +3]
    célula = arredonda(normalizado) + 3          ← desloca para [0, 6]
    célula = limita(célula, 0, 6)                ← garante dentro do grid
```

---

## 7. A Órbita — 8 Features que Descrevem a "Forma" do Gesto

Aqui está o coração da teoria espacial. Em vez de comparar ponto-a-ponto (como DTW tradicional), o sistema extrai **8 características contínuas** que descrevem a **forma orbital** do movimento — inspirado na **Análise de Movimento de Laban** (Effort factors):

```
┌─────────────────────────────────────────────────────────────────┐
│              AS 8 FEATURES DA ASSINATURA ORBITAL                │
├─────────────┬───────────────────────┬───────────────────────────┤
│  FEATURE    │  O QUE MEDE           │  ANALOGIA ORBITAL        │
├─────────────┼───────────────────────┼───────────────────────────┤
│             │                       │                           │
│ ① Amplitude │  Energia média do     │  Quão longe o cometa     │
│    W=1.5    │  movimento (média     │  viaja do sol em média   │
│             │  da aceleração)       │                           │
│             │                       │                           │
│ ② Pico     │  Aceleração máxima    │  Ponto mais distante     │
│    W=1.0    │  atingida             │  da órbita (apélio)      │
│             │                       │                           │
│ ③ Lineari- │  O movimento é reto   │  Órbita elíptica (1.0)   │
│   dade      │  ou circular?         │  vs circular (0.0)       │
│    W=2.0 ★  │  (PCA eigenvalues)    │  ← MAIS IMPORTANTE      │
│             │                       │                           │
│ ④ Plano    │  Em que plano o       │  Inclinação do plano     │
│   Normal    │  gesto acontece?      │  orbital                 │
│    W=0.7    │  [nx, ny, nz]         │                           │
│             │                       │                           │
│ ⑤ Duração  │  Quanto tempo durou   │  Período orbital         │
│    W=0.5    │  o stroke (ms)        │                           │
│             │                       │                           │
│ ⑥ Suavidade│  Movimento fluido     │  Órbita estável vs       │
│    W=1.0    │  ou abrupto?          │  caótica                 │
│             │  (inverso do jerk)    │                           │
│             │                       │                           │
│ ⑦ Rotação  │  Proporção de giro    │  Spin do cometa          │
│    W=1.5    │  vs translação        │  (giroscópio)            │
│             │  (gyro / total)       │                           │
│             │                       │                           │
│ ⑧ Simetria │  Primeira metade      │  Órbita espelhada?       │
│    W=1.0    │  = segunda metade     │  (Pearson correlation)   │
│             │  invertida?           │                           │
└─────────────┴───────────────────────┴───────────────────────────┘

★ Linearidade tem peso 2.0 — é a feature mais discriminativa
  porque separa gestos retos (acenar) de circulares (girar pulso)
```

### Detalhamento de Cada Feature

#### ① Amplitude (W=1.5)

```
    Média de quão longe cada amostra está do repouso.

    repouso ──●────── ← amostra 1 (desvio = 0.3g)
              │
    repouso ──●──────────── ← amostra 2 (desvio = 0.6g)
              │
    repouso ──●──── ← amostra 3 (desvio = 0.2g)

    amplitude = média(0.3, 0.6, 0.2) = 0.37g
    normalizado = 0.37 / 2.0 = 0.185
```

#### ② Pico (W=1.0)

```
    Aceleração máxima atingida durante o stroke.

    │              ★ pico = 1.2g
    │             ╱ \
    │            ╱   \
    │           ╱     \
    │──────────╱───────\──────── repouso
    └─────────────────────────→ tempo

    normalizado = 1.2 / 4.0 = 0.30
```

#### ③ Linearidade (W=2.0) — A Mais Importante

Usa **Análise de Componentes Principais (PCA)** para entender a "forma" da nuvem de pontos 3D:

```
    Dados brutos (nuvem de pontos 3D):

         Y
         ↑    · ·
         │   ·  ·  ·                     PCA encontra os eixos
         │  ·    ·    ·     ──────→      principais da nuvem
         │ ·      ·     ·
         │·        ·       ·
         └──────────────────→ X
        ╱
       Z

    RESULTADO DA PCA:

         Y
         ↑
         │        λ₁ (pequeno)
         │        ↑
         │        │   ╱ λ₀ (GRANDE)     λ₀ >> λ₁ >> λ₂
         │        │  ╱                   = gesto LINEAR
         │        │ ╱
         │        │╱───────→             λ₀ ≈ λ₁ >> λ₂
         │                               = gesto PLANAR
         └──────────────────→ X
        ╱                                λ₀ ≈ λ₁ ≈ λ₂
       Z                                 = gesto ESFÉRICO

    Eigenvalues = "quanto a nuvem se espalha" em cada direção
    Calculados pela fórmula de Cardano (O(1), sem iteração!)
```

**Comparação visual:**

```
   LINEARIDADE ALTA (≈0.9)              LINEARIDADE BAIXA (≈0.2)
   Gesto RETO                           Gesto CIRCULAR

        ●→→→→→→→→●                         ●→→→↓
        (vai e volta)                      ↑     ↓
                                           ↑     ↓
        Eigenvalues:                       ←←←←↙
        λ₀=0.90  λ₁=0.05  λ₂=0.05
                                           Eigenvalues:
        linearidade =                      λ₀=0.50  λ₁=0.45  λ₂=0.05
        (0.90 - 0.05) / 0.90 = 0.94
                                           linearidade =
                                           (0.50 - 0.45) / 0.50 = 0.10
```

#### ④ Plano Normal (W=0.7)

```
        Y                          Y                      Y
        ↑    ←→ (gesto lateral)    ↑    ↕ (gesto vert.)   ↑
        │    Normal = [0,1,0]      │    Normal = [1,0,0]   │  ↗↙ (gesto diagonal)
        │    (aponta pra cima)     │    (aponta pro lado)  │  Normal = [0,0,1]
        └──→ X                     └──→ X                  └──→ X
            ╱                          ╱                       ╱
           Z                          Z                       Z

    O plano normal é o eigenvector do menor eigenvalue (λ₂).
    Indica a direção PERPENDICULAR ao plano onde o gesto acontece.
    Peso baixo (0.7) porque a orientação do sensor na mão varia.
```

#### ⑤ Duração (W=0.5)

```
    duração = (offsetIdx - onsetIdx) × 20ms

    Exemplo: onset no sample 20, offset no sample 100
    duração = (100 - 20) × 20ms = 1600ms

    normalizado = 1600 / 3000 = 0.53

    Peso baixo (0.5) porque a velocidade do usuário varia naturalmente.
```

#### ⑥ Suavidade (W=1.0)

```
    Baseada no JERK (derivada da aceleração = "solavanco")

    Gesto SUAVE:                        Gesto ABRUPTO:
    │    ╱──╲                           │   ╱|
    │   ╱    ╲                          │  ╱ |╲
    │  ╱      ╲                         │ ╱  | ╲╱╲
    │ ╱        ╲                        │╱   |    ╲
    └────────────→                      └────────────→
    jerk baixo → suavidade alta         jerk alto → suavidade baixa

    suavidade = 1.0 / (1.0 + média_jerk × 10)

    Jerk = 0.02 → suavidade = 1/(1+0.2) = 0.83 (fluido)
    Jerk = 0.15 → suavidade = 1/(1+1.5) = 0.40 (abrupto)
```

#### ⑦ Rotação (W=1.5)

```
    Proporção entre energia rotacional (giroscópio) e translacional (acelerômetro).

    GESTO DE TRANSLAÇÃO:               GESTO DE ROTAÇÃO:
    (mover a mão pro lado)             (girar o pulso)

    ───→ ───→ ───→                      ↻ ↻ ↻

    accel alto, gyro baixo              accel baixo, gyro alto
    rotação ≈ 0.1                       rotação ≈ 0.8

    rotação = energiaGyro / (energiaGyro + energiaAccel)

    Peso alto (1.5) porque ajuda muito a distinguir
    gestos de torção vs gestos lineares.
```

#### ⑧ Simetria (W=1.0)

```
    Compara a primeira metade do gesto com a segunda metade invertida.

    GESTO SIMÉTRICO (vai e volta):      GESTO ASSIMÉTRICO:
    │    ╱╲                              │    ╱╲
    │   ╱  ╲                             │   ╱  \
    │  ╱    ╲                            │  ╱    \───
    │ ╱      ╲                           │ ╱
    └──────────→                         └──────────→
    correlação ≈ +0.9                    correlação ≈ -0.3

    Usa correlação de Pearson entre:
    - magnitudes da 1ª metade
    - magnitudes da 2ª metade (invertida)

    Range: [-1, +1]
    normalizado dividindo por RANGE_SYMMETRY (2.0)
```

---

## 8. Matching — Comparando Órbitas

Quando o usuário faz um gesto, o sistema compara a **assinatura orbital capturada** com cada **gesto treinado** no banco:

```
    GESTO CAPTURADO                    GESTO TREINADO "água"
    ┌──────────────┐                   ┌──────────────┐
    │ amp  = 0.70  │                   │ amp  = 0.65  │
    │ pico = 1.20  │    distância     │ pico = 1.15  │
    │ lin  = 0.86  │ ◄═══════════════► │ lin  = 0.84  │
    │ dur  = 1600  │   Euclidiana      │ dur  = 1550  │
    │ suav = 0.33  │   ponderada       │ suav = 0.35  │
    │ rot  = 0.12  │                   │ rot  = 0.10  │
    │ sim  = 0.45  │                   │ sim  = 0.50  │
    │ plano=[...]  │                   │ plano=[...]  │
    └──────────────┘                   └──────────────┘
```

### Fórmula da Distância

```
distância = √(
    1.5 × (Δamplitude / 2.0)²   +     ← peso amplitude
    1.0 × (Δpico / 4.0)²        +     ← peso pico
    2.0 × (Δlinearidade / 1.0)² +     ← peso linearidade (MAIOR)
    0.7 × (Δplano_angular)²     +     ← peso plano normal
    0.5 × (Δduração / 3000)²    +     ← peso duração (MENOR)
    1.0 × (Δsuavidade / 1.0)²   +     ← peso suavidade
    1.5 × (Δrotação / 1.0)²     +     ← peso rotação
    1.0 × (Δsimetria / 2.0)²          ← peso simetria
)
```

### Conversão para Score

```
    score = 1.0 / (1.0 + distância)

    distância = 0.0  →  score = 1.00  (idêntico)
    distância = 0.15 →  score = 0.87  (muito similar)
    distância = 0.82 →  score = 0.55  (limite de aceitação)
    distância = 3.0  →  score = 0.25  (muito diferente)

    ┌─────────────────────────────────────────────────┐
    │  score ≥ 0.55  →  ✅ MATCH! → Toca o áudio     │
    │  score < 0.55  →  ❌ Não reconhecido            │
    └─────────────────────────────────────────────────┘
```

### Combinação dos Dois Sensores

```
    score_final = 0.7 × score_mão_direita + 0.3 × score_mão_esquerda
                       ↑                          ↑
                  mão dominante              mão auxiliar
                  (mais peso)                (menos peso)
```

### Detecção de Ambiguidade

```
    Se o 2º melhor score está dentro de 0.5 do melhor:

    Gesto A: score = 0.72  ← melhor
    Gesto B: score = 0.68  ← segundo (diferença = 0.04 < 0.5)

    → AMBÍGUO: sistema pode pedir confirmação
```

---

## 9. Treinamento — Como o Sistema Aprende um Gesto

O treinamento coleta **3 amostras** do mesmo gesto e calcula a média:

```
    AMOSTRA 1                AMOSTRA 2                AMOSTRA 3
    ┌──────────┐             ┌──────────┐             ┌──────────┐
    │ amp=0.68 │             │ amp=0.63 │             │ amp=0.64 │
    │ lin=0.85 │             │ lin=0.82 │             │ lin=0.85 │
    │ rot=0.11 │             │ rot=0.09 │             │ rot=0.10 │
    │ ...      │             │ ...      │             │ ...      │
    └──────────┘             └──────────┘             └──────────┘
          │                       │                        │
          └───────────┬───────────┘────────────────────────┘
                      │
                      ▼  MÉDIA
               ┌──────────┐
               │ amp=0.65 │  ← Assinatura treinada
               │ lin=0.84 │
               │ rot=0.10 │
               │ ...      │
               └──────────┘

    Threshold = maior_distância_entre_pares × 1.5
    (tolerância baseada na variabilidade natural do usuário)
```

---

## 10. Duas Mãos — Construção de Frases

O Gestuum usa **dois sensores** para construir frases completas:

```
    MÃO ESQUERDA (Sensor B)          MÃO DIREITA (Sensor A)
    ┌─────────────────┐              ┌─────────────────┐
    │   CONTEXTO      │              │   OBJETO         │
    │   (prefixo)     │              │   (substantivo)  │
    │                 │              │                  │
    │  "eu quero"     │              │  "água"          │
    │  "eu preciso"   │      +       │  "comida"        │
    │  "por favor"    │              │  "ajuda"         │
    └────────┬────────┘              └────────┬─────────┘
             │                                │
             └────────────┬───────────────────┘
                          │
                          ▼
                ┌─────────────────┐
                │  FRASE COMPLETA │
                │                 │
                │ 🔊 "eu quero    │
                │     água"       │
                └─────────────────┘
```

### Fluxo Temporal da Construção de Frase

```
    ──────────────────────────────────────────────────────→ tempo

    │ double-tap │  gesto B   │  espera  │  gesto A  │  áudio
    │            │  contexto  │   2s     │  objeto   │
    │            │  detecta   │  janela  │  detecta  │
    │            │ "quero"    │          │  "água"   │
    │            │            │          │           │
    ▼            ▼            ▼          ▼           ▼
    IDLE → RECORDING → CONTEXT_WAIT → RECORDING → PLAY
                          (2 seg)
```

---

## 11. Exemplo Completo: Reconhecendo "Água"

### Setup

- Dois M5StickC Plus2: Sensor A (mão direita), Sensor B (mão esquerda)
- Gesto "água" treinado com 3 amostras
- Nível: STANDARD (double-tap 2.2g, mínimo 2000ms de gravação)

### Execução Passo a Passo

```
t=0ms     │ Double-tap no Sensor A (pico de 2.5g > threshold 2.2g)
          │ → Estado muda para RECORDING
          │
t=0-300ms │ Cooldown — 15 amostras ignoradas (vibração do double-tap)
          │ → Calibra magnitude de repouso com amostras 15-20
          │
t=300ms   │ Amostras 15-20: repouso calibrado = [0.3, 0.2, 1.0]g
          │ (magnitude ≈ 1.07g = gravidade + orientação do sensor)
          │
t=400ms   │ Amostra 20: accel = [0.8, 0.5, 1.2]g
          │ desvio do repouso = 0.5g > 0.3g (onset threshold)
          │ → Contador onset = 1/3
          │
t=440ms   │ Amostra 22: desvio = 0.6g > 0.3g
          │ → Contador onset = 3/3 → ONSET DETECTADO!
          │ → onsetIdx = 20 (retroativo)
          │
t=440ms-  │ Amostras 22-100: trajetória do gesto
2000ms    │ Acelerômetro captura cada movimento da mão
          │ Grid 7×7×7 registra células visitadas
          │ Buffer raw acumula dados para extração orbital
          │
t=2000ms  │ Amostra 100: accel retorna próximo ao repouso
          │ desvio = 0.12g < 0.15g (offset threshold)
          │ → Contador offset = 1/25
          │
t=2500ms  │ Amostra 125: 25 amostras consecutivas < 0.15g
          │ → OFFSET DETECTADO!
          │ → offsetIdx = 100 (retroativo)
          │
          ▼
    ┌─────────────────────────────────────────────┐
    │           EXTRAÇÃO ORBITAL                   │
    │                                              │
    │  Amostras 20-100 (onset → offset)            │
    │                                              │
    │  1. Subtrai gravidade de todas as amostras   │
    │  2. Calcula covariância 3×3                  │
    │  3. Cardano → eigenvalues [0.85, 0.12, 0.03]│
    │  4. Linearidade = (0.85-0.12)/0.85 = 0.86   │
    │  5. Calcula amplitude, pico, suavidade...    │
    │                                              │
    │  Resultado:                                  │
    │  ┌────────────────────────┐                  │
    │  │ amplitude  = 0.70     │                  │
    │  │ pico       = 1.20     │                  │
    │  │ linearidade = 0.86    │                  │
    │  │ duração    = 1600ms   │                  │
    │  │ suavidade  = 0.33     │                  │
    │  │ rotação    = 0.12     │                  │
    │  │ simetria   = 0.45     │                  │
    │  │ plano      = [nx,ny,nz]│                  │
    │  └────────────────────────┘                  │
    └─────────────────────────────────────────────┘
          │
          ▼
    ┌─────────────────────────────────────────────┐
    │              MATCHING                        │
    │                                              │
    │  vs "água" treinado:                         │
    │  {amp:0.65, pico:1.15, lin:0.84, dur:1550}   │
    │                                              │
    │  distância = √(pesos × diferenças²) ≈ 0.15  │
    │  score = 1/(1+0.15) = 0.87                   │
    │                                              │
    │  0.87 ≥ 0.55 (threshold) → ✅ MATCH!        │
    └─────────────────────────────────────────────┘
          │
          ▼
    ┌─────────────────────────────────────────────┐
    │  🔊 Reproduz: /voices/homem/agua.wav         │
    │     via I2S (11025 Hz, 16-bit mono)          │
    │                                              │
    │  Usuário ouve: "água"                        │
    └─────────────────────────────────────────────┘
```

---

## 12. Por que a Teoria Orbital? Comparação com DTW Tradicional

```
    ABORDAGEM TRADICIONAL (DTW)         ABORDAGEM ORBITAL (Gestuum)
    ───────────────────────────         ───────────────────────────

    Compara PONTO A PONTO:              Compara a FORMA da órbita:

    ● → ● → ● → ● → ●                 8 números descrevem
    ↕   ↕   ↕   ↕   ↕                  TODA a forma do gesto:
    ● → ● → ● → ● → ●                 amplitude, pico, linearidade,
                                        plano, duração, suavidade,
    Alinha cada ponto no                rotação, simetria
    tempo (elastic matching)

    ❌ ~91KB por comparação             ✅ ~36 bytes por assinatura
    ❌ O(N×M) computação                ✅ O(1) extração (Cardano)
    ❌ Sensível à velocidade            ✅ Robusto à velocidade
    ❌ Lento no ESP32                   ✅ Rápido no ESP32
    ❌ Precisa alinhar tempo            ✅ Tempo-invariante
```

### Resumo dos Recursos do ESP32

| Recurso | Disponível | Uso do Orbital |
|---------|------------|----------------|
| RAM     | 520KB      | ~5KB por gravação |
| CPU     | 240MHz     | Cardano O(1), sem iteração |
| Flash   | 4-16MB     | Assinaturas: 36 bytes/gesto |

A teoria espacial/orbital permite que o ESP32 — com apenas **520KB de RAM** — reconheça gestos em tempo real, comparando **formas de movimento** em vez de sequências brutas de pontos.

---

## 13. Constantes do Sistema — Referência Rápida

### Parâmetros do Sensor

| Parâmetro | Valor | Descrição |
|-----------|-------|-----------|
| `IMU_SAMPLE_RATE_HZ` | 50 | Taxa de amostragem |
| `IMU_SAMPLE_PERIOD_MS` | 20 | Período entre amostras |
| `EMA_ALPHA` | 0.3 | Agressividade do filtro |
| `DAMPING_FACTOR` | 0.95 | Amortecimento da posição |
| `ACCEL_RANGE` | 2.0g | Range do acelerômetro |
| `GYRO_RANGE` | 200 dps | Range do giroscópio |

### Parâmetros do Grid

| Parâmetro | Valor | Descrição |
|-----------|-------|-----------|
| `GRID_SIZE` | 7 | Dimensão do cubo (7×7×7) |
| `GRID_CENTER` | 3 | Índice central |
| `MIN_GESTURE_MOVEMENT` | 3.0 | Distância mínima no grid |
| `DTW_MIN_TRAJECTORY_LEN` | 3 | Mínimo de pontos na trajetória |
| `DTW_MAX_TRAJECTORY_LEN` | 150 | Máximo de pontos (FIFO) |

### Pesos das Features Orbitais

| Feature | Peso | Justificativa |
|---------|------|---------------|
| Linearidade | **2.0** | Mais discriminativa (reto vs circular) |
| Amplitude | 1.5 | Energia do movimento |
| Rotação | 1.5 | Separa torção de translação |
| Pico | 1.0 | Intensidade máxima |
| Suavidade | 1.0 | Fluido vs abrupto |
| Simetria | 1.0 | Padrões espelhados |
| Plano Normal | 0.7 | Reduzido (orientação do sensor varia) |
| Duração | **0.5** | Menos informativa (velocidade varia) |

### Níveis de Dificuldade

| Nível | Min Gravação | Timeout | Double-Tap | Trajetória Min |
|-------|-------------|---------|------------|----------------|
| LIMITED | 1000ms | 4000ms | 1.8g | 2 pontos |
| STANDARD | 2000ms | 5000ms | 2.2g | 3 pontos |
| ADVANCED | 2500ms | 7000ms | 3.0g | 5 pontos |

---

## 14. Arquivos-Fonte Relevantes

| Arquivo | Responsabilidade |
|---------|-----------------|
| `orbital_extractor.cpp` | Eigendecomposição + 8 features |
| `matrix3d.cpp` | Grid 7×7×7 + detecção de stroke |
| `gesture_engine.cpp` | Lógica de reconhecimento + matching |
| `dtw.cpp` | DTW com rolling array (fallback) |
| `imu_reader.cpp` | Leitura IMU + filtro EMA |
| `constants.h` | Todos os thresholds e enums |

---

> **Fundamentação teórica:** Análise de Movimento de Laban (Effort factors) + PCA via fórmula de Cardano (solução analítica O(1))
>
> **Grau de Confiança:** ALTO — baseado diretamente no código-fonte do projeto
