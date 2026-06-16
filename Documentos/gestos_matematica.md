# GESTUUM — A Matematica por tras dos Gestos

> Documento tecnico para explicar como o reconhecimento de gestos funciona
> do ponto de vista matematico. Inclui o que temos hoje (v1) e o que muda
> na versao 2 (v2) para maior precisao.

---

## 1. O Problema: Transformar Movimento em Palavras

O GESTUUM converte **gestos de mao** em **voz sintetizada**. Para isso, precisa
resolver um problema matematico: dado um movimento no espaco 3D, identificar
qual gesto foi feito entre N gestos possiveis.

O sensor (MPU6886) mede duas coisas 50 vezes por segundo:

```
Acelerometro: a(t) = [ax, ay, az]    (forca em cada eixo, incluindo gravidade)
Giroscopio:   g(t) = [gx, gy, gz]    (rotacao em cada eixo)
```

Cada gesto gera uma **serie temporal** de ~75-150 amostras (1.5-3 segundos).
O desafio e comparar essa serie com templates treinados e encontrar o mais parecido.

---

## 2. Pipeline Atual (v1): Grid 3D + Assinaturas Orbitais

### 2.1 Discretizacao em Grid 3D (Matrix3D)

O primeiro passo e mapear a aceleracao continua para uma **grade discreta 11x11x11**.

**Formula de mapeamento:**

```
celula(eixo) = round((a_eixo / ACCEL_RANGE + 0.5) * (GRID_SIZE - 1))
             = round((a_eixo / 2.0 + 0.5) * 10)

Onde:
  a_eixo = aceleracao no eixo (g), range [-2g, +2g]
  ACCEL_RANGE = 2.0g
  GRID_SIZE = 11
  Resultado: celula inteira [0, 10], centro = 5
```

**Exemplo numerico:**

```
Mao parada:    a = [0.0, 0.0, 1.0]g   → celula [5, 5, 8]
                                         (gravidade puxa Z para cima na grid)

Mao para frente: a = [0.5, 0.0, 1.0]g → celula [8, 5, 8]
                                         (X sobe de 5 para 8)

Mao para cima:   a = [0.0, 0.5, 1.0]g → celula [5, 8, 8]
                                         (Y sobe de 5 para 8)
```

A sequencia de celulas visitadas forma a **trajetoria** do gesto:

```
Gesto "I want" (CX01):
  [5,5,5] → [6,5,5] → [7,5,5] → [7,5,6] → [7,5,5]
  
  Inicio (repouso) → move no eixo X → sobe Z → volta
```

**Representacao grafica:** Ver imagem `docs/gestures/CX01_I want.png`

![Trajetoria CX01 I want](gestures/CX01_I%20want.png)

A imagem mostra 4 vistas da trajetoria no cubo 11x11x11:
- **3D**: cubo com a trajetoria desenhada (verde=inicio, vermelho=fim)
- **Vista Superior (X-Y)**: movimento lateral vs frente/tras
- **Vista Lateral (X-Z)**: movimento lateral vs cima/baixo
- **Vista Frontal (Y-Z)**: frente/tras vs cima/baixo

### 2.2 Assinaturas Orbitais (OrbitalExtractor)

A trajetoria na grid captura **onde** o gesto foi, mas perde informacao sobre
**como** foi feito (rapido/lento, suave/brusco, linear/circular). Para capturar
a **forma** do movimento, extraimos 8 features matematicas das amostras brutas:

**Feature 1 — Amplitude (media da magnitude):**

```
amplitude = (1/N) * sum(||a(i) - g||)

Onde:
  a(i) = aceleracao na amostra i
  g = gravidade estimada (media das amostras em repouso)
  ||x|| = sqrt(x² + y² + z²)   (norma euclidiana)
  N = numero de amostras do stroke
```

Gestos amplos (bracos abertos) tem amplitude alta (~2.0g).
Gestos sutis (pulso) tem amplitude baixa (~0.3g).

**Feature 2 — Pico (magnitude maxima):**

```
peak = max(||a(i) - g||)   para todo i no stroke
```

**Feature 3 — Linearidade (PCA):**

Analise de Componentes Principais (PCA) 3x3 sobre as amostras do stroke.
Calcula os 3 autovalores (lambda1 >= lambda2 >= lambda3) da matriz de covariancia:

```
Covariancia C = (1/N) * sum((a(i) - media) * (a(i) - media)^T)

Linearidade = (lambda1 - lambda2) / (lambda1 + epsilon)
```

- Linearidade ~1.0: movimento em linha reta (lambda1 >> lambda2)
- Linearidade ~0.0: movimento circular ou espalhado (lambda1 ≈ lambda2)

**Feature 4 — Plano Normal:**

O autovetor do **menor** autovalor (lambda3) indica a direcao de menor variancia
— ou seja, o **plano** em que o movimento ocorreu.

```
planoNormal = autovetor(lambda3)
```

Se o gesto e feito no plano horizontal, planoNormal ≈ [0, 0, 1] (vertical).
Se no plano vertical frontal, planoNormal ≈ [0, 1, 0] (para frente).

**Feature 5 — Duracao:**

```
duracao = N_stroke * dt = N_stroke * 20ms   (50Hz)
```

**Feature 6 — Suavidade (inverso do jerk):**

O jerk e a derivada da aceleracao — quao brusco e o movimento:

```
jerk_medio = (1/(N-1)) * sum(||a(i) - a(i-1)||)

suavidade = 1 / (1 + jerk_medio * 10)
```

- Suavidade ~1.0: movimento muito suave (danca, tai chi)
- Suavidade ~0.3: movimento brusco (soco, tap)

**Feature 7 — Rotacao (energia do giroscopio):**

```
rotacao = energia_gyro / (energia_gyro + energia_accel + epsilon)

energia_gyro = sum(gx² + gy² + gz²)
energia_accel = sum((ax-gx)² + (ay-gy)² + (az-gz)²)
```

- Rotacao ~0.0: movimento translacional (sem girar o pulso)
- Rotacao ~1.0: movimento rotacional (girar a mao)

**Feature 8 — Simetria (correlacao de Pearson):**

Compara a 1a metade do gesto com a 2a metade invertida:

```
simetria = correlacao(magnitudes_1a_metade, magnitudes_2a_metade_invertida)
```

- Simetria ~1.0: movimento simetrico (ida e volta iguais)
- Simetria ~-1.0: movimento assimetrico (ida diferente da volta)
- Simetria ~0.0: sem correlacao

### 2.3 Comparacao: Distancia Ponderada

Para comparar duas assinaturas (capturada vs template), usamos distancia
euclidiana ponderada e normalizada:

```
distancia = sqrt(
    W_amp  * ((amp_a - amp_b) / R_amp)²  +
    W_peak * ((peak_a - peak_b) / R_peak)² +
    W_lin  * ((lin_a - lin_b) / R_lin)²  +
    W_plane * (1 - |dot(plano_a, plano_b)|)² +
    W_dur  * ((dur_a - dur_b) / R_dur)²  +
    W_sm   * ((sm_a - sm_b) / R_sm)²    +
    W_rot  * ((rot_a - rot_b) / R_rot)² +
    W_sym  * ((sym_a - sym_b) / R_sym)²
)
```

**Pesos calibrados:**

| Feature | Peso (W) | Range (R) | Justificativa |
|---------|----------|-----------|---------------|
| Amplitude | 1.5 | 2.0g | Tamanho do gesto |
| Pico | 1.0 | 4.0g | Intensidade maxima |
| **Linearidade** | **2.0** | 1.0 | **Mais discriminativo** — forma do movimento |
| Plano | 0.7 | angular | Orientacao (reduzido por variacao do pulso) |
| Duracao | 0.5 | 3000ms | Velocidade (menos peso por variacao natural) |
| Suavidade | 1.0 | 1.0 | Brusquidade |
| **Rotacao** | **1.5** | 1.0 | **Muito discriminativo** — gyro vs accel |
| Simetria | 1.0 | 2.0 | Ida-volta vs unidirecional |

A distancia e convertida em score (0-100%):

```
score = distanceToScore(d) = 1.0 / (1.0 + d * d)

d = 0   → score = 100% (identico)
d = 0.5 → score = 80%
d = 1.0 → score = 50%
d = 2.0 → score = 20%
```

### 2.4 Decisao: Threshold + Ratio Test

```
Score combinado = 70% * score_acelerometro + 30% * score_giroscopio

Aceitar se:
  1. score_combinado >= 55% (threshold por gesto)
  2. ratio(second_best / best) <= 0.95 (ratio test de Lowe, 2004)
     → se dois gestos tem scores muito proximos, rejeitar como ambiguo
```

**Representacao grafica:** Ver `docs/gestures/G01_water.png`

![Trajetoria G01 water](gestures/G01_water.png)

---

## 3. Limitacao do Pipeline v1: Direcao

### O Problema Fundamental

O acelerometro mede **aceleracao**, nao **posicao**. Quando a mao se move
para frente e depois para tras (movimentos opostos), o perfil de aceleracao
e **simetrico**:

```
Para frente:  a(t) = [0, 0, +2, +3, +2, 0, -2, -3, -2, 0]
                      repouso  acelera        freia     repouso

Para tras:    a(t) = [0, 0, -2, -3, -2, 0, +2, +3, +2, 0]
                      repouso  acelera        freia     repouso
```

Na grid 3D, ambos visitam as **mesmas celulas** (so em ordem diferente).
As assinaturas orbitais (amplitude, pico, linearidade) sao **identicas**
porque medem magnitude, nao direcao.

**Resultado:** Gestos opostos no mesmo eixo sao **indistinguiveis** no v1.

### Evidencia nos Dados Reais

Nos testes, os gestos CX01 "I want" e CX02 "I don't want" geravam
trajetorias quase identicas na grid:

```
CX01: [5,5,5] → [6,5,5] → [7,5,5] → [7,5,6]
CX02: [5,5,5] → [6,5,5] → [7,5,5] → [7,5,6]
```

A distincao so era possivel pelo giroscopio (rotacao diferente do pulso).

---

## 4. Pipeline v2: Peak Direction + DTW + Orbital

### 4.1 Peak Direction (3 bits)

Para cada eixo (X, Y, Z), detectar o **sinal do primeiro pico** de aceleracao:

```
Para cada eixo e:
  Percorrer amostras do stroke
  Encontrar primeiro valor |a_e(i)| > threshold
  Se a_e(i) > 0: direcao_e = +1 (positivo)
  Se a_e(i) < 0: direcao_e = -1 (negativo)
  Senao:         direcao_e = 0  (sem movimento significativo)

Resultado: vetor de 3 valores {+1, -1, 0} = 27 combinacoes possiveis
```

**Exemplo:**

```
"Para frente": primeiro pico em X positivo → direcao = {+1, 0, 0}
"Para tras":   primeiro pico em X negativo → direcao = {-1, 0, 0}
→ Trivialmente distinguiveis pelo sinal!
```

**Custo computacional:** O(N) por eixo. Microsegundos. Zero memoria.

### 4.2 DTW — Dynamic Time Warping

DTW compara a **forma completa** da curva de aceleracao, nao features resumidas.
Permite que o gesto seja feito em velocidades diferentes (warping temporal).

**Formula (programacao dinamica):**

```
D(0,0) = d(Q[0], C[0])
D(i,0) = infinito  para i > 0
D(0,j) = infinito  para j > 0

D(i,j) = d(Q[i], C[j]) + min(D(i-1,j), D(i,j-1), D(i-1,j-1))

Distancia DTW = D(N-1, M-1)

Onde:
  Q = gesto capturado (N amostras)
  C = template treinado (M amostras)
  d(a,b) = |ax-bx| + |ay-by| + |az-bz|  (distancia Manhattan 3D)
```

**Propriedades importantes:**
- **Deterministico:** mesma entrada = mesma saida (nao e probabilistico!)
- **Tolerante a velocidade:** gesto rapido vs lento = mesmo resultado
- **Preserva direcao:** curvas opostas tem distancia DTW alta
- **Complexidade:** O(N * M) — com banda Sakoe-Chiba: O(N * w)

**Exemplo numerico (1 eixo):**

```
Template "frente": [0, +1.5, +3.0, +1.5, 0, -1.5, -3.0, -1.5, 0]
Template "tras":   [0, -1.5, -3.0, -1.5, 0, +1.5, +3.0, +1.5, 0]

Gesto capturado (frente, mais rapido):
                   [+0.2, +2.0, +3.2, +0.8, -0.5, -2.0, -3.5, -0.8]

DTW vs "frente": distancia = 2.1  (baixa — MATCH)
DTW vs "tras":   distancia = 18.5 (alta — nao match)
```

**Viabilidade no ESP32 (240MHz):**

```
N = 75 amostras (1.5s a 50Hz)
w = 8 (banda 10%)
50 templates
Operacoes: 75 * 16 * 50 = 60.000
Tempo: 60.000 / 240.000.000 = 0.25ms
Memoria: 2 linhas * 75 * 4 bytes = 600 bytes
```

### 4.3 Arquitetura Combinada (Bitmap + Vetor)

A analogia com design grafico:

```
DTW (dados brutos)      = BITMAP  — pixel a pixel, toda informacao
Orbital (8 features)    = VETOR   — curvas matematicas, forma abstrata
Peak Direction (3 bits) = FILTRO  — elimina candidatos impossiveis
```

Manter os dois e mais seguro:

```
Captura IMU 50Hz
    |
Subtrair gravidade (filtro complementar)
    |
Peak Direction → elimina ~70% dos templates (direcao oposta)
    |
DTW (forma completa) → "e o mesmo CAMINHO?" (60% do score)
    |
Orbital (8 features) → "e a mesma FORMA?" (40% do score)
    |
Threshold por gesto + Ratio test → aceitar/rejeitar
```

---

## 5. Comparacao v1 vs v2

| Aspecto | v1 (atual) | v2 (planejado) |
|---------|-----------|----------------|
| **Dados de entrada** | Grid discretizada (perde direcao) | Dados brutos (preserva direcao) |
| **Matching principal** | Orbital (8 features resumidas) | DTW (curva completa) |
| **Matching secundario** | sequenceSimilarity | Orbital (validacao) |
| **Pre-filtro** | Rejection por amplitude | Peak Direction (3 bits) |
| **Gestos opostos** | Indistinguiveis | Trivialmente separados |
| **Tolerancia a velocidade** | Parcial (duracao como feature) | Total (warping DTW) |
| **Complexidade** | O(1) por template | O(N*w) por template |
| **Tempo no ESP32** | ~0.1ms | ~0.25ms (50 templates) |
| **Memoria** | ~1.6KB | ~45KB |
| **Precisao estimada (30 gestos)** | ~70% | ~95%+ |

### O que muda na pratica

**v1 — Vocabulario limitado:**
- Cada gesto precisa usar eixo/rotacao diferente
- Gestos naturais como "sim/nao" (cima/baixo) sao indistinguiveis
- Com 30+ gestos, faltam combinacoes unicas

**v2 — Vocabulario natural:**
- Gestos opostos funcionam (frente/tras, cima/baixo)
- O DTW compara a sequencia temporal completa
- 50+ gestos com precisao >95%

---

## 6. Referencias Academicas

| Referencia | Contribuicao |
|------------|-------------|
| **Sakoe & Chiba (1978)** | DTW original — "Dynamic programming algorithm optimization for spoken word recognition", IEEE TASSP |
| **Liu et al. (2009)** | uWave — DTW para gestos com acelerometro, 98.6% acuracia com 8 gestos, IEEE PerCom |
| **Lowe (2004)** | Ratio test — "Distinctive Image Features from Scale-Invariant Keypoints", IJCV |
| **Keogh & Ratanamahatana (2005)** | Otimizacoes DTW (LB_Keogh bound), Knowledge and Information Systems |
| **Madgwick (2010)** | Filtro de fusao IMU — estimativa de orientacao para subtracao de gravidade |

---

## 7. Resumo para Apresentacao

O GESTUUM usa **matematica aplicada** para converter gestos em fala:

1. **Amostragem** — sensor captura 50 vetores 3D por segundo (150 numeros/s)
2. **Discretizacao** — mapeia aceleracao continua para grid 11x11x11 (funcao piso)
3. **Extracao de features** — PCA 3x3, correlacao de Pearson, norma euclidiana
4. **Comparacao** — distancia ponderada normalizada entre vetores de 8 dimensoes
5. **Decisao** — threshold + ratio test (estatistica descritiva)
6. **Evolucao (v2)** — programacao dinamica (DTW) para comparacao de series temporais

A matematica envolvida abrange: **algebra linear** (PCA, autovalores), **calculo**
(integracao, derivada/jerk), **estatistica** (correlacao, media, desvio padrao),
**programacao dinamica** (DTW), e **geometria analitica** (distancia euclidiana,
projecoes 3D, planos normais).

O projeto demonstra que a matematica nao e abstrata — ela **da voz a quem nao tem**.
