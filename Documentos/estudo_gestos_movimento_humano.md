# GESTUUM — Estudo Multidisciplinar de Gestos e Movimento Humano

**Projeto:** GESTUUM — Seus gestos, sua voz
**Contexto:** Science Fair COREE 2026 — Programa IB
**Data:** 2026-03-29
**Propósito:** Base teórica para inovar no reconhecimento de gestos, saindo do paradigma puramente tecnológico para um entendimento profundo do movimento humano. Este documento é referência acadêmica para o projeto do IB.

---

## Índice

1. [Por que este estudo existe](#1-por-que-este-estudo-existe)
2. [Biomecânica do braço humano](#2-biomecânica-do-braço-humano)
3. [Linguística das línguas de sinais](#3-linguística-das-línguas-de-sinais)
4. [Taxonomia de gestos — Kendon e McNeill](#4-taxonomia-de-gestos--kendon-e-mcneill)
5. [Sistemas históricos de comunicação gestual](#5-sistemas-históricos-de-comunicação-gestual)
6. [Sistemas de movimento não-convencionais](#6-sistemas-de-movimento-não-convencionais)
7. [Ciência cognitiva dos gestos](#7-ciência-cognitiva-dos-gestos)
8. [Aprendizado motor e acessibilidade](#8-aprendizado-motor-e-acessibilidade)
9. [Fases do gesto — anatomia temporal](#9-fases-do-gesto--anatomia-temporal)
10. [Design de gestos — o que a pesquisa diz](#10-design-de-gestos--o-que-a-pesquisa-diz)
11. [Estado da arte — IMU e reconhecimento](#11-estado-da-arte--imu-e-reconhecimento)
12. [INOVAÇÃO — Modelo Orbital de Trajetória](#12-inovação--modelo-orbital-de-trajetória)
13. [Composicionalidade — duas mãos, vocabulário exponencial](#13-composicionalidade--duas-mãos-vocabulário-exponencial)
14. [Síntese — princípios para o GESTUUM](#14-síntese--princípios-para-o-gestuum)
15. [Fontes](#15-fontes)

---

## 1. Por que este estudo existe

O GESTUUM enfrenta um problema real: falsos positivos no reconhecimento de gestos. O motor atual (Grid 6D com comparação por sequência) discretiza o movimento em células de uma matriz 3D e tenta casar ponto a ponto. O resultado: gestos diferentes fazem match, gestos iguais falham por variação de timing.

**A raiz do problema não é tecnológica — é conceitual.** Estávamos olhando o movimento como dados numa matriz, quando deveríamos estar olhando o movimento como **expressão humana com estrutura própria**.

Este estudo investiga:
- Como o corpo humano se move (biomecânica)
- Como civilizações codificaram gestos em linguagem (história)
- Como a ciência decompõe gestos em partes (linguística, cinesiologia)
- Como artes marciais e dança ensinam movimento (sistemas não-convencionais)
- Como o cérebro aprende e lembra gestos (cognição)
- Como tudo isso pode transformar nosso reconhecimento de gestos

**Objetivo final:** encontrar uma abordagem de reconhecimento que capture a *essência* do gesto — sua forma, seu plano, sua intenção — não apenas os pontos que ele percorre.

---

## 2. Biomecânica do braço humano

### 2.1 Graus de liberdade (DoF)

O braço humano é uma estrutura cinemática com **7 graus de liberdade principais**:

| Articulação | DoF | Movimentos |
|-------------|-----|------------|
| Ombro | 3 | Flexão/extensão, abdução/adução, rotação interna/externa |
| Cotovelo | 2 | Flexão/extensão, pronação/supinação (antebraço) |
| Pulso | 2 | Flexão/extensão, desvio radial/ulnar |
| **Total** | **7** | |

Considerando a escápula (3 DoF adicionais) e os dedos (4 DoF por dedo), o membro superior tem **~27 DoF** no total.

> **Fonte:** Pieper, D.L. (1968). "The Kinematics of Manipulators Under Computer Control." Stanford University. Confirmado em PMC: "A Vexing Question in Motor Control: The Degrees of Freedom Problem" (Frontiers in Bioengineering, 2021).

### 2.2 O problema dos graus de liberdade (Bernstein)

Nikolai Bernstein (1896-1966) formulou um dos problemas centrais da neurociência motora: **como o cérebro controla tantos graus de liberdade simultaneamente?**

A solução que Bernstein propôs — e que a pesquisa moderna confirmou — é o conceito de **freezing e freeing** (congelar e liberar):

1. **Fase de congelamento (novato):** O aprendiz *congela* graus de liberdade, reduzindo a complexidade. Um novato segurando uma espada move o braço inteiro como uma unidade rígida — ombro, cotovelo e pulso se movem juntos.

2. **Fase de liberação (intermediário):** Gradualmente, graus de liberdade são *liberados*. O cotovelo começa a se mover independente do ombro.

3. **Fase de exploração (expert):** O expert usa *sinergias motoras* — acoplamentos flexíveis entre articulações que se adaptam ao contexto. Um espadachim expert usa o pulso para ajustes finos enquanto o ombro controla a direção geral.

> **Fonte:** Bernstein, N.A. (1967). "The Co-ordination and Regulation of Movements." Confirmado em: Vereijken et al. (1992). "Free(z)ing Degrees of Freedom in Skill Acquisition." Journal of Motor Behavior, 24(1).

### 2.3 Implicação para o GESTUUM

**Insight crítico:** Se o GESTUUM pede gestos que exigem controle independente de pulso + cotovelo + ombro, o usuário **novato vai congelar** essas articulações e fazer um movimento rígido do braço inteiro. Gestos que dependem de rotação isolada do pulso, por exemplo, são **inacessíveis para novatos**.

**Regra de design derivada:** Gestos do GESTUUM devem usar **movimentos de braço inteiro** (que qualquer pessoa faz naturalmente) em vez de movimentos articulares isolados (que exigem treino).

### 2.4 Reprodutibilidade de movimentos

Nem todos os movimentos são igualmente reproduzíveis:

| Tipo de movimento | Reprodutibilidade | Por quê |
|---|---|---|
| Movimentos balísticos (empurrar, apontar) | **Alta** | Controlados por programas motores pré-computados |
| Movimentos cíclicos (acenar, circular) | **Alta** | Controlados por geradores de padrão central (CPG) |
| Movimentos de precisão (desenhar letra) | **Baixa** | Requerem feedback visual contínuo |
| Movimentos lentos e controlados | **Média** | Dependem de propriocepção, que varia |

> **Fonte:** Schmidt, R.A. & Lee, T.D. (2011). "Motor Control and Learning." Human Kinetics, 5ª edição.

**Implicação para o GESTUUM:** Priorizar **movimentos balísticos e cíclicos** — são os mais reproduzíveis e os mais naturais para humanos.

---

## 3. Linguística das línguas de sinais

### 3.1 A descoberta de Stokoe — gestos têm "fonemas"

Em 1960, William Stokoe publicou uma descoberta revolucionária: a American Sign Language (ASL) não é mímica — é uma **língua com estrutura fonológica**, assim como línguas faladas.

Stokoe demonstrou que cada sinal pode ser decomposto em unidades mínimas que ele chamou de **cheremes** (do grego *kheir*, "mão"), análogos aos fonemas das línguas faladas.

### 3.2 Os 5 parâmetros das línguas de sinais

A pesquisa moderna identifica **5 parâmetros** que definem qualquer sinal:

| Parâmetro | O que define | Exemplo (ASL) |
|-----------|-------------|---------------|
| **Configuração de mão** (handshape) | Formato dos dedos e mão | Mão aberta vs punho fechado vs apontar |
| **Localização** (location) | Onde a mão está no espaço ou no corpo | Na testa vs no peito vs no espaço neutro |
| **Movimento** (movement) | Direção, velocidade, repetição | Para cima vs circular vs reto |
| **Orientação da palma** (palm orientation) | Para onde a palma aponta | Palma para baixo vs para cima vs para fora |
| **Marcadores não-manuais** (non-manual) | Expressão facial, postura | Sobrancelha levantada = pergunta |

> **Fonte:** Stokoe, W.C. (1960). "Sign Language Structure." Confirmado em: Brentari, D. (1998). "A Prosodic Model of Sign Language Phonology." PMC: "The Phonological Organization of Sign Languages" (2013).

### 3.3 Pares mínimos — a prova da estrutura

Assim como "bat" e "pat" diferem por um fonema, sinais em ASL diferem por um parâmetro:

- **MOTHER** vs **FATHER** → mesma configuração, mesmo movimento, localização diferente (queixo vs testa)
- **CHAIR** vs **SIT** → mesma configuração, mesmo movimento, número de mãos diferente

### 3.4 Libras — a língua brasileira de sinais

Libras segue a mesma estrutura de 5 parâmetros. Inventário documentado:

| Parâmetro | Quantidade em Libras |
|-----------|---------------------|
| Configurações de mão | 111 (Madson & Barreto, 2012) |
| Localizações | 35 pontos (Barros) |
| Movimentos | Variados (direção, velocidade, repetição) |
| Orientações | 6 direções básicas |
| Marcadores não-manuais | Expressões faciais, postura, olhar |

> **Fonte:** Quadros, R.M. & Karnopp, L.B. (2004). "Língua de Sinais Brasileira: Estudos Linguísticos." Artmed.

### 3.5 Implicação para o GESTUUM

**Insight crítico:** Se línguas de sinais funcionam com 5 parâmetros, o GESTUUM não precisa capturar *todo* o gesto — precisa capturar os **parâmetros que distinguem um gesto do outro**.

Com IMUs (acelerômetro + giroscópio), conseguimos medir:
- ✅ **Movimento** (aceleração, direção, velocidade)
- ✅ **Orientação** (giroscópio → rotação do pulso/mão)
- ⚠️ **Localização** (parcial — pela integração de aceleração, com drift)
- ❌ **Configuração de mão** (não temos sensores nos dedos)
- ❌ **Marcadores não-manuais** (não temos câmera)

**Conclusão:** O GESTUUM opera com **2 dos 5 parâmetros** (movimento + orientação). Isso limita o vocabulário, mas é suficiente se o design dos gestos explorar bem esses 2 parâmetros.

---

## 4. Taxonomia de gestos — Kendon e McNeill

### 4.1 O Continuum de Kendon

Adam Kendon propôs um espectro que vai de gestos espontâneos a línguas completas:

```
Gesticulação → Gestos ligados à fala → Pantomima → Emblemas → Língua de sinais
     ↑                                                              ↑
  Involuntário,                                              Estrutura gramatical
  acompanha fala                                             completa, sem fala
```

À medida que avançamos no continuum:
- A dependência da fala **diminui**
- A convencionalidade **aumenta**
- A estrutura linguística **aumenta**

> **Fonte:** Kendon, A. (2004). "Gesture: Visible Action as Utterance." Cambridge University Press.

### 4.2 Classificação de McNeill

David McNeill classificou os gestos que acompanham a fala em 4 tipos:

| Tipo | Descrição | Exemplo | Relevância para GESTUUM |
|------|-----------|---------|------------------------|
| **Icônico** | Representa objetos/ações concretas | Mão imitando beber água | ⭐⭐⭐ Alta — intuitivo, fácil de lembrar |
| **Metafórico** | Representa conceitos abstratos | Mãos se afastando = "crescimento" | ⭐⭐ Média — requer convenção |
| **Dêitico** | Apontar (real ou imaginário) | Dedo apontando para um objeto | ⭐ Baixa — requer contexto visual |
| **Beat** | Rítmico, marca ênfase | Mão batendo para baixo no ritmo | ⭐ Baixa — não carrega significado |

> **Fonte:** McNeill, D. (1992). "Hand and Mind: What Gestures Reveal about Thought." University of Chicago Press.

### 4.3 Implicação para o GESTUUM

**Os gestos icônicos são o caminho.** São os mais intuitivos, os mais fáceis de lembrar, e os mais naturais entre culturas. Um gesto de "beber água" que imita o ato de levar um copo à boca é universalmente compreensível.

**Regra de design:** Os gestos do GESTUUM devem ser **icônicos sempre que possível** — imitar a ação real que representam. Quando não for possível (conceitos abstratos), usar **metáforas corporais** intuitivas.

---

## 5. Sistemas históricos de comunicação gestual

A humanidade criou sistemas de comunicação gestual em contextos surpreendentemente diversos. Cada um oferece lições para o GESTUUM.

### 5.1 Chironomia — Grécia e Roma antigas

**O que era:** Um sistema codificado de gestos de mão usado na retórica e oratória. O termo vem do grego *cheir* (mão) + *nomos* (lei) — literalmente, "a lei do gesto."

**Como funcionava:** Quintiliano (35-100 d.C.), no tratado *Institutio Oratoria*, documentou dezenas de gestos com significados convencionados. Os gestos dos oradores eram tão codificados que formavam "uma linguagem inteira que o orador deve dominar além das palavras."

**Princípios relevantes:**
- Gestos tinham **significados convencionais** compartilhados entre oradores e audiência
- A **moderação** era essencial — gestos exagerados perdiam credibilidade
- Cada gesto precisava ser **distinto e claro** para uma audiência a distância

**Lição para o GESTUUM:** Gestos não precisam ser complexos para carregar significado. Os romanos comunicavam conceitos sofisticados com **gestos de mão simples e distintos**. A clareza importa mais que a complexidade.

> **Fonte:** Quintiliano. "Institutio Oratoria" (c. 95 d.C.), Livro XI, Cap. 3. Confirmado em: ResearchGate — "Cicero and Quintilian on the oratorical use of hand gestures."

### 5.2 Mudras — Dança clássica indiana

**O que são:** Sistema de gestos de mão (*hasta mudras*) usado no Bharatanatyam, Kathakali e outras danças clássicas indianas. Documentados no *Natya Shastra* de Bharata Muni (~200 a.C. — 200 d.C.).

**Estrutura do sistema:**

| Categoria | Quantidade | Descrição |
|-----------|-----------|-----------|
| Asamyuta Hasta (uma mão) | 28 | Gestos com uma mão só |
| Samyuta Hasta (duas mãos) | 23 | Gestos com as duas mãos |
| Nritta Hasta (dança pura) | 27 | Gestos decorativos/rítmicos |
| **Total** | **~62-78** | Variando por tradição |

**Princípios relevantes:**
- Com apenas **28 gestos de uma mão**, o sistema expressa centenas de significados dependendo do contexto
- A **combinação** de gestos simples gera complexidade (composicionalidade)
- Cada mudra tem um nome e significado fixo, mas pode representar coisas diferentes em contextos diferentes

**Lição para o GESTUUM:** Um vocabulário pequeno de gestos base (~15-20) pode gerar um vocabulário grande por **combinação e contexto** (categorias). Não precisamos de 100 gestos diferentes — precisamos de 15 gestos compostos com categorias.

> **Fonte:** Bharata Muni, *Natya Shastra*. Confirmado em: Wikipedia — "List of mudras (dance)"; Sahapedia — "Mudra: Various Aspects and Dimensions."

### 5.3 Língua de sinais monástica — Silêncio como restrição

**O que era:** Sistemas de gestos usados em mosteiros beneditinos e cistercienses da Europa desde o século X. Monges sob voto de silêncio usavam gestos para comunicação essencial.

**Estrutura do sistema:**
- Vocabulário de **52 a 472 sinais** (média ~178, mediana ~145)
- Predominância de **substantivos concretos**: comida, roupas, cômodos, objetos rituais
- Sinais **não formavam gramática** — eram um léxico gestual, não uma língua completa

**Princípios relevantes:**
- Sistema criado sob **restrição severa** (silêncio obrigatório) — exatamente como o GESTUUM (sem fala)
- Vocabulário focado em **necessidades práticas do dia-a-dia**
- Gestos **icônicos e simples** — um monge novo aprendia em dias, não meses

**Lição para o GESTUUM:** Quando a restrição é clara (não pode falar), um léxico gestual de ~150 sinais cobre as necessidades diárias. O GESTUUM não precisa ser uma língua — precisa ser um **léxico eficaz**.

> **Fonte:** Wikipedia — "Monastic sign languages." Medievalists.net — "The Origins of Cistercian Sign Language."

### 5.4 Plains Indian Sign Language (PISL) — Comunicação universal entre tribos

**O que era:** Uma língua de sinais usada como *lingua franca* entre mais de 110.000 indígenas de mais de 500 tribos diferentes na América do Norte (documentado em 1885).

**Por que é extraordinário:**
- Funcionava **entre culturas completamente diferentes** que não falavam a mesma língua
- Usada para **comércio, diplomacia, cerimônias, e comunicação com surdos**
- Gestos eram **suficientemente intuitivos** para serem aprendidos por falantes de qualquer língua

**Princípios relevantes:**
- Gestos eram predominantemente **icônicos** — representavam a coisa em si
- O sistema era **aprendido em dias** de convívio, não em meses de estudo
- Funcionava **sem contexto cultural compartilhado** — a iconicidade transcendia culturas

**Lição para o GESTUUM:** Se gestos icônicos suficientemente intuitivos funcionam entre culturas completamente diferentes, eles funcionarão para qualquer usuário do GESTUUM. **Iconicidade = universalidade.**

> **Fonte:** Britannica — "Plains Indian sign language (PISL)." Wikipedia — "Plains Indian Sign Language."

### 5.5 Línguas de sinais aborígenes australianas

**O que são:** Sistemas de sinais usados por povos indígenas australianos (Yolŋu, Warlpiri, entre outros) tanto por ouvintes quanto por surdos.

**Contexto de uso:**
- Período de **luto** (silêncio obrigatório para mulheres viúvas — até 2 anos)
- Comunicação à **distância** (campo aberto)
- **Caça** (silêncio necessário)
- Cerimonial (silêncio ritualístico)
- Comunicação com **surdos** da comunidade

**Princípio relevante:** Os aborígenes australianos praticam **bilinguismo bimodal** — alternam naturalmente entre fala e gestos dependendo do contexto. O gesto não é substituto inferior da fala — é um **canal paralelo** igualmente válido.

**Lição para o GESTUUM:** Gestos não são "fala de segunda classe." São um **modo legítimo de comunicação** que culturas inteiras usam por escolha, não apenas por limitação.

> **Fonte:** AIATSIS — "Yolŋu Sign Language." Wikipedia — "Australian Aboriginal sign languages."

### 5.6 Sinais militares

**O que são:** Gestos padronizados usados pelo exército (codificados no TC 3-21.60 do U.S. Army) para comunicação silenciosa em campo.

**Princípios relevantes:**
- Gestos são **grandes e visíveis** (feitos com braço inteiro, não dedos)
- Cada gesto é **inequívoco** — confusão pode ser fatal
- O vocabulário é **limitado e funcional** (~40-60 sinais operacionais)
- Aprendidos em **treinamento básico** (semanas, não meses)

**Lição para o GESTUUM:** Para comunicação crítica (emergência no GESTUUM), os gestos devem ser **grandes, distintos e impossíveis de confundir**. Nada de movimentos sutis para "socorro."

> **Fonte:** U.S. Army TC 3-21.60 "Visual Signals" (2017).

---

## 6. Sistemas de movimento não-convencionais

### 6.1 Tai Chi — O princípio da unidade corporal

O Tai Chi oferece uma filosofia de movimento diametralmente oposta à robótica (que isola articulações). Os 10 princípios de Yang Chengfu (1883-1936) incluem:

**"A vibração de qualquer movimento de Tai Chi enraíza nos pés, libera pelas pernas, é controlada pela cintura, move-se pelas costas e braços, e finalmente é expressa pelos dedos."**

**"O corpo inteiro deve se mover como uma unidade completa. Quando uma parte do corpo se move, todas as partes devem estar em movimento."**

**Princípios relevantes para o GESTUUM:**

| Princípio do Tai Chi | Implicação para o GESTUUM |
|---|---|
| Corpo move como unidade | Gestos devem envolver braço inteiro, não só pulso |
| Movimento enraíza nos pés | Gestos sentados (cadeira de rodas) têm base diferente |
| Fluxo contínuo, sem brusquidão | Movimentos suaves são mais reproduzíveis que abruptos |
| Lentidão é controle | Gestos lentos e deliberados são mais distinguíveis |
| Relaxamento antes de força | Músculos tensos geram tremor — gestos devem ser naturais |

**Lição radical:** O Tai Chi prova que **movimentos lentos e suaves** carregam mais informação que movimentos bruscos e rápidos. Um sensor que captura um gesto lento e controlado terá dados mais limpos que um que tenta capturar um sacudão.

> **Fonte:** Yang Chengfu, "The Essence of Tai Chi Chuan" (1934). Confirmado em: kung-fu.co.za — "10 Principles of Tai Chi Chuan."

### 6.2 Laban Movement Analysis (LMA) — A linguagem do movimento

Rudolf Laban (1879-1958) criou o sistema mais completo de análise de movimento humano, originalmente para dança, mas aplicado em terapia, ergonomia, robótica e reconhecimento de gestos.

**Os 4 componentes BESS:**

| Componente | O que descreve | Subcomponentes |
|---|---|---|
| **Body** (Corpo) | Quais partes se movem, como se conectam | Parte ativa, suporte, iniciação |
| **Effort** (Esforço) | Qualidade dinâmica do movimento — a *intenção* | Space, Weight, Time, Flow |
| **Shape** (Forma) | Como o corpo muda de forma no espaço | Abrir/fechar, subir/descer, avançar/recuar |
| **Space** (Espaço) | Caminhos e padrões espaciais | Direções, planos, níveis, kinesfera |

**O componente Effort é o mais relevante para IMUs:**

| Fator de Effort | Polaridades | O que a IMU captura |
|---|---|---|
| **Space** (Espaço) | Direto ↔ Flexível | Trajetória reta vs curva (accel) |
| **Weight** (Peso) | Forte ↔ Leve | Magnitude da aceleração |
| **Time** (Tempo) | Súbito ↔ Sustentado | Jerk (derivada da aceleração) |
| **Flow** (Fluxo) | Contido ↔ Livre | Variabilidade/suavidade do sinal |

**Resultados em reconhecimento de gestos:**
- Estudo usando LMA + HMM (Hidden Markov Models) alcançou **>92% de acurácia** para 11 gestos diferentes
- Outro estudo usando Space + Effort de LMA alcançou **~99% de acurácia** para movimentos de dança

**Lição para o GESTUUM:** Em vez de comparar trajetórias brutas, extrair os **fatores de Effort de Laban** dos dados do IMU. Um gesto de "água" tem Effort diferente de um gesto de "socorro" — mesmo que as trajetórias se pareçam na grid.

> **Fonte:** Laban, R. (1966). "Choreutics." Confirmado em: Springer — "Laban movement analysis and hidden Markov models for dynamic 3D gesture recognition" (EURASIP, 2017). IJSTR — "Dance Gesture Recognition Using Space Component and Effort Component of LMA" (2020).

### 6.3 Labanotação — Notação universal de movimento

Laban também criou um sistema de **notação escrita para movimento** — o equivalente a partituras musicais, mas para o corpo.

**Como funciona:**
- Uma pauta vertical representa o tempo (de baixo para cima)
- Colunas representam diferentes partes do corpo
- Símbolos abstratos codificam: **direção, nível (alto/médio/baixo), duração e qualidade**
- O sistema registra 4 dimensões: **forma do símbolo = direção, sombreamento = nível, comprimento = duração, posição na pauta = parte do corpo**

**Aplicações além da dança:** Fisioterapia, esportes (natação, patinação), ergonomia industrial, zoologia (dança nupcial de aranhas!).

**Lição para o GESTUUM:** Se é possível representar QUALQUER movimento humano com um sistema de notação finito (direção + nível + duração + parte do corpo), então é possível representar gestos do GESTUUM com um conjunto finito de **descritores abstratos**. Não precisamos de dados brutos — precisamos de uma **notação compacta do movimento**.

> **Fonte:** Britannica — "Labanotation." Wikipedia — "Labanotation."

### 6.4 Regência orquestral — Gesto como informação contínua

O maestro de orquestra controla ~100 músicos usando apenas gestos. A mão direita (com batuta) marca o tempo; a mão esquerda comunica **expressividade**.

**Princípios relevantes:**

| Aspecto do gesto | Informação transmitida |
|---|---|
| Amplitude do movimento | Volume (piano vs forte) |
| Velocidade/suavidade | Articulação (staccato vs legato) |
| Altura da mão | Dinâmica (crescendo/decrescendo) |
| Tensão vs relaxamento | Intensidade emocional |
| Precisão vs fluidez | Caráter da música |

**Insight para reconhecimento:** Um maestro NÃO faz gestos geometricamente idênticos a cada compasso. O padrão de 4/4 varia em amplitude, velocidade e forma — mas os músicos entendem porque reconhecem a **essência qualitativa** (Effort, na linguagem de Laban), não a forma geométrica exata.

**Lição para o GESTUUM:** O reconhecimento deve tolerar variação geométrica e capturar **qualidades dinâmicas** (forte/leve, rápido/lento, direto/curvo).

> **Fonte:** Springer — "A Grammar of Expressive Conducting Gestures" (2024).

---

## 7. Ciência cognitiva dos gestos

### 7.1 Cognição corporificada (Embodied Cognition)

A teoria da cognição corporificada afirma que **o corpo não é apenas executor do pensamento — ele participa do pensamento**. Gestos não são subprodutos da fala; são parte do processo cognitivo.

**Evidências:**
- Pessoas que gesticulam enquanto explicam problemas matemáticos resolvem **mais rápido** do que quando impedidas de gesticular
- Crianças expostas a pais que gesticulam frequentemente desenvolvem **vocabulário maior**
- Observar gestos ativa as **mesmas áreas cerebrais** que fazer os gestos (neurônios-espelho)

> **Fonte:** Stanford Encyclopedia of Philosophy — "Embodied Cognition." Springer — "Research Avenues Supporting Embodied Cognition in Learning and Instruction" (2024).

### 7.2 Neurônios-espelho e aprendizado de gestos

Neurônios-espelho disparam tanto quando **fazemos** uma ação quanto quando **observamos** alguém fazendo a mesma ação. Isso tem implicações diretas:

- **Demonstração é o melhor ensino:** Mostrar um gesto para o usuário ativar automaticamente os circuitos motores para reproduzi-lo
- **Gestos que imitam ações reais** (icônicos) ativam mais neurônios-espelho que gestos abstratos
- A memória para gestos é **mais forte quando o gesto é significativo** (tem relação com o conceito)

### 7.3 Carga cognitiva e complexidade gestual

**Gestos simples** requerem menos recursos cognitivos, permitindo que o usuário se concentre na **comunicação** em vez da **execução**. Quando um gesto é muito complexo, a atenção vai para "estou fazendo certo?" em vez de "o que quero dizer?"

**Implicação direta:** Para acessibilidade, **menos é mais**. Um gesto que o usuário faz sem pensar é infinitamente melhor que um gesto que ele precisa se concentrar para executar.

### 7.4 Propriocepção — saber onde está o corpo sem olhar

Propriocepção é a capacidade de sentir a posição e o movimento do próprio corpo sem input visual. Pesquisa mostra:

- **Fadiga muscular degrada a propriocepção** — quanto mais cansado, menos preciso o gesto
- **Propriocepção é melhor para movimentos grandes** (braço inteiro) do que para movimentos finos (dedos)
- Pessoas com deficiência motora podem ter **propriocepção alterada** — gestos dependentes de posição precisa são problemáticos

> **Fonte:** PMC — "Proprioceptive loss and the perception, control and learning of arm movements in humans" (2018). Wayne State University — "Effects of Local Muscle Fatigue on Proprioception and Motor Learning."

**Lição para o GESTUUM:** Gestos devem funcionar **sem feedback visual**. O usuário deve conseguir fazê-los olhando para a pessoa com quem está se comunicando, não para os sensores.

---

## 8. Aprendizado motor e acessibilidade

### 8.1 As 3 fases de Fitts e Posner

O modelo mais aceito de aprendizado motor:

| Fase | Nome | Comportamento | Duração |
|------|------|---------------|---------|
| 1 | **Cognitiva** | O aprendiz pensa cada passo conscientemente. Erros frequentes. Movimentos rígidos. | Minutos a horas |
| 2 | **Associativa** | Refinamento. Menos erros. Começa a "sentir" o movimento. | Dias a semanas |
| 3 | **Autônoma** | Movimento automático, sem pensar. Performance consistente. | Semanas a meses |

> **Fonte:** Fitts, P.M. & Posner, M.I. (1967). "Human Performance." Brooks/Cole.

**Implicação para o GESTUUM:** O sistema precisa funcionar **na fase 1** (cognitiva) — quando o usuário ainda está pensando no gesto. Se o reconhecimento só funciona para gestos de expert (fase 3), o sistema é inútil para a maioria dos usuários.

### 8.2 Lei de Fitts — Speed-Accuracy Tradeoff

A Lei de Fitts (1954) estabelece que o tempo para alcançar um alvo é proporcional à distância e inversamente proporcional ao tamanho do alvo:

**T = a + b × log₂(2D/W)**

Onde D = distância e W = largura do alvo.

**Tradução para gestos:** Quanto mais "preciso" o gesto precisa ser (alvo pequeno na grid), mais lento o usuário fará. Isso explica por que grids com células pequenas (11x11x11) falham — o "alvo" é pequeno demais.

**Lição:** Aumentar o tamanho da "zona de aceitação" de cada gesto. É melhor ter 10 gestos com zonas grandes do que 50 gestos com zonas minúsculas.

### 8.3 Síndrome do Gorilla Arm — Fadiga gestual

O termo "gorilla arm" descreve a fadiga muscular causada por **manter braços elevados** por períodos prolongados. Originalmente descrito para telas touchscreen verticais.

**Pesquisa:**
- Após **poucos minutos** de gestos no ar, usuários relatam desconforto
- Fadiga piora a **precisão** dos gestos e degrada a **propriocepção**
- Movimentos no **nível da cintura ou abaixo** causam muito menos fadiga que movimentos acima do ombro
- Gestos com **mãos simétricas** (duas mãos fazendo o mesmo) causam menos fadiga percebida

> **Fonte:** Springer — "Dispelling the Gorilla Arm Syndrome" (2017). MPI — "Modeling the Gorilla Arm Effect" (2021).

**Lição para o GESTUUM:** Gestos devem ser executáveis com **braços relaxados** (na altura da cintura ou abaixo). Nada de "levante o braço acima da cabeça para pedir ajuda" — isso é insustentável.

### 8.4 Acessibilidade e deficiência motora

Para o público-alvo do GESTUUM (pessoas com dificuldade de fala, potencialmente com limitações motoras):

| Limitação | O que afeta | Design adaptativo |
|---|---|---|
| Espasticidade | Movimentos involuntários, precisão reduzida | Zonas de aceitação grandes, filtrar tremor |
| Fadiga rápida | Não sustenta movimentos longos | Gestos curtos (<3 segundos) |
| Amplitude reduzida | Movimentos pequenos | Normalizar amplitude (não medir absoluto) |
| Coordenação bilateral prejudicada | Dificuldade com 2 mãos simultâneas | Permitir gestos de 1 mão só |
| Propriocepção alterada | Não sabe onde a mão está sem olhar | Não depender de posição absoluta |

---

## 9. Fases do gesto — Anatomia temporal

### 9.1 As fases de Kendon

Todo gesto tem uma estrutura temporal:

```
[REPOUSO] → [PREPARAÇÃO] → [STROKE] → [HOLD (opcional)] → [RETRAÇÃO] → [REPOUSO]
              ↑                ↑              ↑                 ↑
           Mão sai do       Fase que       Pausa no           Mão volta
           repouso          carrega        ponto              ao repouso
                            significado    extremo
```

| Fase | Característica | O que a IMU vê |
|------|---------------|----------------|
| **Repouso** | Mão parada (no colo, na mesa) | Aceleração ≈ (0, 0, 1g) constante |
| **Preparação** | Mão se move para posição inicial | Aceleração crescente, geralmente para cima |
| **Stroke** | O gesto em si — carrega todo o significado | Padrão de aceleração característico |
| **Hold** | Pausa no ponto extremo | Aceleração ≈ 0 (mas posição ≠ repouso) |
| **Retração** | Volta ao repouso | Aceleração inversa, geralmente para baixo |

> **Fonte:** Kendon, A. (2004). Confirmado em: Bressem & Ladewig (2011) — "Rethinking gesture phases: Articulatory features." ScienceDirect — "Gesture phase segmentation using support vector machines" (2016).

### 9.2 O Stroke é tudo

A pesquisa é clara: **o stroke é a única fase que carrega significado**. Preparação e retração são apenas logística — levar a mão até o ponto inicial e devolvê-la.

**Implicação para reconhecimento:**
- O reconhecimento deve focar **APENAS no stroke**, ignorando preparação e retração
- A **detecção automática do stroke** é um problema de segmentação já bem estudado (F-score ~0.8-0.9 em coders humanos)
- Pesquisa mostra que preparação é predominantemente **movimento para cima** e retração é **movimento para baixo** — o IMU pode detectar essas tendências

**Insight:** Nosso motor atual compara **o gesto inteiro** (preparação + stroke + retração). Se conseguirmos isolar o stroke, a comparação será muito mais precisa.

### 9.3 Segmentação por aceleração

A transição entre fases tem assinatura clara no sinal de aceleração:

```
Repouso → Preparação: aceleração sai de ~0 (ONSET — detectável por threshold)
Preparação → Stroke: mudança de direção ou aceleração (TRANSIÇÃO)
Stroke → Hold: aceleração volta a ~0 (mas giroscópio pode indicar orientação diferente do repouso)
Hold → Retração: aceleração retoma (mas em direção oposta à preparação)
Retração → Repouso: aceleração volta a ~0 (END — detectável por threshold)
```

---

## 10. Design de gestos — O que a pesquisa diz

### 10.1 Gestos definidos pelo usuário (Wobbrock, 2009)

Jacob Wobbrock (University of Washington) conduziu um estudo seminal: em vez de *designers* criarem gestos, pediu que **usuários** propusessem gestos para ações desejadas.

**Descobertas:**
- Gestos definidos por usuários são **até 24% mais fáceis de lembrar** do que gestos definidos por designers
- Usuários **convergem** — diferentes pessoas propõem gestos semelhantes para a mesma ação
- O conceito de **guessability** (adivinhabilidade): um gesto bom é aquele que o usuário "adivinha" sem instrução

> **Fonte:** Wobbrock, J.O. et al. (2009). "User-Defined Gestures for Surface Computing." ACM CHI.

### 10.2 Princípios de design de gestos

A literatura consolida estes princípios:

| Princípio | Descrição | Como aplicar no GESTUUM |
|---|---|---|
| **Iconicidade** | Gesto lembra a ação | "Beber" = levar mão à boca |
| **Simplicidade** | Menos articulações envolvidas, melhor | Braço inteiro > pulso isolado |
| **Distinção** | Gestos devem ser maximamente diferentes entre si | Planos diferentes (horizontal vs vertical) |
| **Conforto** | Sem posições extremas ou fadiga | Abaixo do ombro, relaxado |
| **Reversibilidade** | Fácil de cancelar/recomeçar | Voltar ao repouso = cancelar |
| **Escalabilidade** | Funciona em diferentes amplitudes | Normalizar amplitude |
| **Learnability** | Aprendido em minutos, não horas | Máximo 10-15 gestos iniciais |
| **Guessability** | Intuitivo sem instrução | Metáfora corporal clara |

### 10.3 Tamanho ideal do vocabulário

| Sistema | Tamanho do vocabulário | Contexto |
|---|---|---|
| Sinais militares | ~40-60 | Comunicação tática |
| Sinais monásticos (mediana) | ~145 | Vida monástica diária |
| PISL (Plains Indian) | ~1000+ | Comunicação intertribal completa |
| ASL/Libras | ~10.000+ | Língua natural completa |
| **GESTUUM (proposta)** | **20-30 por categoria** | Comunicação assistiva básica |

A literatura sugere que **50-100 gestos** cobrem as necessidades comunicativas básicas diárias. Com 5 categorias de ~20 gestos cada, o GESTUUM cobriria esse range.

---

## 11. Estado da arte — IMU e reconhecimento

### 11.1 O que funciona com IMU de pulso

| Abordagem | Acurácia | Gestos | Fonte |
|---|---|---|---|
| PCA + classificador | 97% | 11 gestos, 100 usuários | Kela et al. (2006) |
| IMU + áudio (wrist) | 92.6% | 8 gestos no ar | PLoS ONE (2019) |
| EMG + IMU no antebraço | 88.8% | 12 gestos | ArXiv (2025) |
| LMA + HMM | >92% | 11 ações | EURASIP (2017) |
| Jerk features + Fourier | Alta | Head gestures | ResearchGate (2020) |

### 11.2 Features que funcionam (estado da arte)

A pesquisa identifica features mais discriminativas para IMU:

| Feature | O que captura | Como extrair |
|---|---|---|
| **Magnitude média da aceleração** | Intensidade geral | `mean(sqrt(ax²+ay²+az²))` |
| **Jerk (derivada da aceleração)** | Suavidade vs brusquidão | `diff(accel)/dt` |
| **Zero-crossing rate** | Mudanças de direção | Contar mudanças de sinal |
| **Energia por eixo** | Plano dominante de movimento | `sum(ax²)`, `sum(ay²)`, `sum(az²)` |
| **PCA — componente principal** | Direção dominante do gesto | Eigenvector do covariance matrix |
| **Duração do stroke** | Comprimento temporal | Tempo entre onset e offset |
| **Ratio gyro/accel** | Rotação vs translação | Energia giroscópio / energia aceleração |

### 11.3 O que o GESTUUM faz hoje vs. o que deveria fazer

| Aspecto | Abordagem atual (Grid 6D) | Abordagem informada pela pesquisa |
|---|---|---|
| Representação | Grid 7×7×7 discretizada | Features contínuas (amplitude, plano, curvatura) |
| Comparação | Sequência ponto-a-ponto | Distância entre vetores de features |
| Tolerância | ±1 célula (vizinhança) | Normalização de amplitude + threshold por feature |
| Tempo | Sensível a timing | Normalizado por duração |
| O que compara | "Onde o gesto passou" | "Como o gesto se comportou" (Effort) |

---

## 12. INOVAÇÃO — Modelo Orbital de Trajetória

### 12.1 A ideia

**Premissa:** Em vez de tratar o gesto como uma sequência de pontos numa grid 3D, tratá-lo como uma **trajetória no espaço** — como um cometa orbitando um sol.

O "sol" é o **ponto de repouso** do sensor (aceleração gravitacional pura: ~0, ~0, ~1g). Quando o usuário faz um gesto, o vetor de aceleração **se desvia** do repouso e traça uma trajetória no espaço 3D de aceleração.

### 12.2 Mapeamento conceitual

| Conceito Orbital | Mapeamento para Gesto | O que captura |
|---|---|---|
| **Sol** (centro gravitacional) | Ponto de repouso (0, 0, 1g) | Referência fixa |
| **Trajetória do cometa** | Caminho do vetor (ax, ay, az) no tempo | A forma do gesto |
| **Semi-eixo maior** (*a*) | Distância média do repouso | **Amplitude** do gesto |
| **Excentricidade** (*e*) | Quão alongada é a trajetória | **Linearidade** (e≈1 = reto, e≈0 = circular) |
| **Inclinação** (*i*) | Ângulo do plano da trajetória | **Plano do movimento** (horizontal/vertical/diagonal) |
| **Período orbital** (*T*) | Duração de um ciclo | **Ritmo** do gesto |
| **Velocidade no periélio** | Velocidade máxima no ponto mais próximo | **Intensidade** do gesto |
| **Momento angular** (*L*) | Conservação do momento | **Rotacionalidade** (translação vs rotação) |

### 12.3 Os "elementos orbitais" de um gesto

Para cada gesto, extraímos 6-8 parâmetros que formam sua **assinatura orbital**:

```
Assinatura Orbital do Gesto = {
    amplitude,       // Semi-eixo maior — quão longe do repouso
    linearidade,     // Excentricidade — reto (1.0) vs circular (0.0)
    plano,           // Inclinação — vetor normal ao plano principal
    duracao,         // Período — tempo do stroke
    intensidade,     // Velocidade máxima — pico de aceleração
    suavidade,       // Jerk médio — quão "redondo" é o movimento
    rotacao,         // Momento angular — quanta rotação (gyro) vs translação
    simetria         // Espelhamento temporal — ida ≈ volta?
}
```

### 12.4 Como extrair de dados IMU reais

**Passo 1 — Isolar o stroke** (ver Seção 9)
- Detectar onset: magnitude da aceleração > threshold (ex: 0.3g acima do repouso)
- Detectar offset: magnitude volta ao repouso por >200ms
- Descartar preparação e retração

**Passo 2 — Subtrair gravidade**
- Remover o componente gravitacional (1g no eixo Z)
- Resultado: aceleração "pura" do movimento

**Passo 3 — Calcular features orbitais**

```
amplitude = mean(magnitude(accel_pura))           // distância média do "sol"
pico = max(magnitude(accel_pura))                  // periélio
```

```
// PCA para encontrar o plano principal
covMatrix = covariance(ax, ay, az)
eigenvectors = eigendecomposition(covMatrix)
plano_normal = eigenvector[2]  // menor variância = normal ao plano
plano_energia = eigenvalue[0] / sum(eigenvalues)  // quanta energia no plano principal

// Linearidade: se eigenvalue[0] >> eigenvalue[1], movimento é linear
// Se eigenvalue[0] ≈ eigenvalue[1], movimento é circular/planar
linearidade = (eigenvalue[0] - eigenvalue[1]) / eigenvalue[0]
```

```
// Suavidade: jerk médio (derivada da aceleração)
jerk = diff(accel) / dt
suavidade = 1.0 / mean(magnitude(jerk))  // mais suave = menos jerk
```

```
// Rotação vs translação
energia_gyro = sum(gx² + gy² + gz²)
energia_accel = sum(ax² + ay² + az²)
rotacao = energia_gyro / (energia_gyro + energia_accel)
```

```
// Simetria temporal: correlação entre 1ª metade e 2ª metade invertida
primeira_metade = accel[0 : N/2]
segunda_metade_invertida = reverse(accel[N/2 : N])
simetria = correlation(primeira_metade, segunda_metade_invertida)
```

### 12.5 Comparação entre gestos

Em vez de comparar sequências ponto-a-ponto, comparamos **vetores de features**:

```
assinatura_treinada = [0.8, 0.3, (0.1, 0.9, 0.2), 1.2, 2.1, 0.7, 0.2, 0.85]
assinatura_lida     = [0.75, 0.35, (0.12, 0.88, 0.22), 1.1, 1.9, 0.65, 0.18, 0.80]

distancia = weighted_euclidean(treinada, lida, pesos)

if distancia < threshold:
    match!
```

**Pesos** por feature permitem ajustar o que importa mais. Se plano e linearidade são os mais discriminativos, damos mais peso a eles.

### 12.6 Por que isso resolve nossos problemas

| Problema atual | Como o modelo orbital resolve |
|---|---|
| **Falso positivo** (gestos diferentes fazem match) | Features orbitais capturam *forma* e *qualidade*, não pontos individuais |
| **Sensível a timing** (mesmo gesto em velocidades diferentes falha) | Duração é UMA feature, não a base da comparação |
| **Sensível a amplitude** (gesto grande vs pequeno falha) | Amplitude pode ser normalizada ou ter peso baixo |
| **Grid resolve mal** (células muito grandes perdem detalhe, muito pequenas são ruidosas) | Não usa grid — features contínuas |
| **Tremor causa ruído** | PCA e média filtram naturalmente o ruído |
| **Interpolação distorce** | Não interpola — extrai features diretamente dos dados |

### 12.7 Viabilidade computacional no ESP32

| Operação | Complexidade | Viável no ESP32? |
|---|---|---|
| Magnitude (sqrt) | O(N) | ✅ Trivial |
| Média, máximo, soma | O(N) | ✅ Trivial |
| Jerk (diff) | O(N) | ✅ Trivial |
| Covariância 3×3 | O(N) | ✅ ~N multiplicações |
| Eigendecomposition 3×3 | O(1) constante | ✅ Fórmula fechada para 3×3 |
| Correlação | O(N) | ✅ ~N multiplicações |
| Distância euclidiana ponderada | O(8) | ✅ 8 subtrações + multiplicações |
| **Total para N=500 amostras** | **~5000 operações** | ✅ **<1ms no ESP32 a 240MHz** |

**Conclusão:** É computacionalmente **mais barato** que a comparação de grid atual (que faz N comparações com tolerância de vizinhança).

### 12.8 Conexão com Laban

Os "elementos orbitais" mapeiam diretamente para os **fatores de Effort de Laban**:

| Elemento orbital | Fator de Effort de Laban |
|---|---|
| Linearidade | **Space** (direto ↔ flexível) |
| Amplitude/pico | **Weight** (forte ↔ leve) |
| Suavidade (jerk) | **Time** (súbito ↔ sustentado) |
| Variabilidade do sinal | **Flow** (contido ↔ livre) |

Isso significa que o modelo orbital não é apenas uma metáfora de engenharia — é fundamentado na **análise de movimento mais validada do mundo** (LMA, usado em terapia, dança, ergonomia e reconhecimento de gestos há 100 anos).

---

## 13. Composicionalidade — Duas mãos, vocabulário exponencial

### 13.1 O princípio

A composicionalidade é o mecanismo pelo qual significados complexos são construídos a partir da **combinação de unidades simples**. É o princípio fundamental de toda língua humana — e a chave para escalar o vocabulário do GESTUUM sem escalar a complexidade.

**A matemática:**

| Abordagem | Gestos aprendidos | Vocabulário resultante | Fator |
|---|---|---|---|
| Mão única | 15 | 15 palavras | ×1 |
| Duas mãos (compostas) | 15 | **15 × 15 = 225** combinações | ×15 |
| Duas mãos + 5 categorias | 15 | **225 × 5 = 1.125** significados | ×75 |

O usuário aprende **15 gestos** e ganha acesso a **mais de 1.000 significados possíveis**.

### 13.2 Arquitetura bimanual do GESTUUM

O GESTUUM já possui dois sensores — um em cada mão. A proposta é atribuir **papéis fixos** a cada sensor:

| Sensor | Mão | Papel | Exemplos de gestos |
|---|---|---|---|
| **Sensor A** (dominante) | Direita* | **Objeto / Substantivo** | água, comida, banheiro, remédio, cama, casa |
| **Sensor B** (secundária) | Esquerda* | **Verbo / Intenção** | quero, não quero, onde, dor, ajuda, obrigado |

*\* Configurável para canhotos — o papel é do sensor, não da mão.*

### 13.3 Exemplos de composição

| Sensor B (intenção) | Sensor A (objeto) | Frase resultante | Áudio |
|---|---|---|---|
| "quero" | "água" | quero água | *"Quero água"* |
| "onde" | "banheiro" | onde é o banheiro | *"Onde é o banheiro?"* |
| "dor" | (gesto na cabeça) | dor de cabeça | *"Estou com dor de cabeça"* |
| "não quero" | "comida" | não quero comer | *"Não quero comer"* |
| "ajuda" | (sem gesto) | pedido de ajuda geral | *"Preciso de ajuda"* |
| "obrigado" | (sem gesto) | agradecimento | *"Obrigado"* |

**Nota:** Gestos de mão única (sem composição) também funcionam — "ajuda" sozinho é válido. A composição é opcional, não obrigatória.

### 13.4 Precedentes históricos — isso já funciona há milênios

| Sistema | Como usa composicionalidade | Resultado |
|---|---|---|
| **Mudras indianas** (Natya Shastra) | 28 gestos de uma mão + 23 de duas mãos | Centenas de significados em dança/narrativa |
| **Libras / ASL** | Sinais compostos (AZUL + CLARO = "azul claro") | Vocabulário ilimitado por composição |
| **PISL** (indígenas americanos) | Combinação de sinais simples entre 500+ tribos | Comunicação intertribal completa |
| **Sinais monásticos** | ~145 sinais cobriam vida diária inteira | Comunicação funcional sob voto de silêncio |
| **Ideogramas chineses** | Radicais combinados (水 água + 口 boca = 渴 sede) | 50.000+ caracteres a partir de ~214 radicais |

### 13.5 Impacto no reconhecimento — simplifica, não complica

A composicionalidade **facilita** o reconhecimento porque:

1. **Cada sensor reconhece independente** — Sensor A compara seus 15-20 gestos, Sensor B compara os seus
2. **Não há gesto "composto"** para treinar — a combinação é feita em software após o match individual
3. **Menos gestos por sensor = menos confusão** — distinguir 15 gestos é muito mais fácil que distinguir 225
4. **Os vocabulários são semanticamente distintos** — verbos (quero/não quero/onde) têm padrões de movimento completamente diferentes de objetos (água/comida/cama)

```
Fluxo de reconhecimento:

Sensor A (mão direita)              Sensor B (mão esquerda)
    │                                    │
    ▼                                    ▼
[Captura IMU]                      [Captura IMU]
    │                                    │
    ▼                                    ▼
[Assinatura orbital]               [Assinatura orbital]
    │                                    │
    ▼                                    ▼
[Match: "água"]                    [Match: "quero"]
    │                                    │
    └──────────┐    ┌───────────────────┘
               ▼    ▼
         [Combinação: "quero" + "água"]
                    │
                    ▼
           [Áudio: "Quero água"]
```

### 13.6 Regras de combinação

| Situação | Comportamento |
|---|---|
| Ambas as mãos fazem gesto | Compõe: `verbo + objeto` → frase |
| Só mão dominante (Sensor A) | Interpreta como objeto/ação isolada: "água" → *"Água"* |
| Só mão secundária (Sensor B) | Interpreta como intenção isolada: "ajuda" → *"Preciso de ajuda"* |
| Nenhuma mão faz gesto | Nada acontece (repouso) |
| Gestos não simultâneos (>1s de diferença) | Trata como dois gestos separados, não compõe |

### 13.7 Janela de simultaneidade

Para compor, os strokes dos dois sensores devem ocorrer dentro de uma **janela temporal**:

- **Janela proposta:** ±1 segundo entre o onset dos dois strokes
- Se os strokes começam com <1s de diferença → **composição**
- Se >1s → **dois gestos independentes** (processados sequencialmente)

Essa janela é ajustável por perfil de acessibilidade (usuários com coordenação bilateral reduzida podem precisar de janela maior).

---

## 14. Síntese — Princípios para o GESTUUM

### 14.1 Princípios de design de gestos derivados deste estudo

| # | Princípio | Origem | Justificativa |
|---|---|---|---|
| 1 | **Gestos de braço inteiro** (não pulso isolado) | Bernstein, Tai Chi | Novatos congelam DoF — braço inteiro é natural |
| 2 | **Movimentos icônicos** (imitam a ação real) | McNeill, PISL, Mudras | Mais intuitivos, fáceis de lembrar, cross-cultural |
| 3 | **Abaixo do ombro** | Gorilla arm research | Previne fadiga, acessível para cadeirantes |
| 4 | **Curtos (<3 segundos de stroke)** | Motor learning, fadiga | Fase autônoma mais rápida, menos carga cognitiva |
| 5 | **Maximamente distintos entre si** | Sign language phonology | Parâmetros diferentes = confusão mínima |
| 6 | **Funcionar na fase cognitiva** | Fitts & Posner | Usuário precisa do sistema ANTES de ser expert |
| 7 | **Não depender de posição absoluta** | Propriocepção, acessibilidade | Funcionar sem olhar, em diferentes posturas |
| 8 | **Vocabulário ≤20 por categoria** | Monásticos, militares | Equilíbrio entre expressividade e learnability |
| 9 | **Reconhecer forma, não pontos** | LMA, modelo orbital | Tolerância natural a variação |
| 10 | **Isolar o stroke** | Kendon, segmentação | Só o stroke carrega significado |

### 14.2 Vocabulário de gestos sugerido (exemplo para categoria GERAL)

Baseado nos princípios acima — gestos icônicos, braço inteiro, maximamente distintos:

| Gesto | Ação icônica | Plano principal | Tipo de movimento |
|---|---|---|---|
| Água | Levar mão à boca (beber) | Sagital (frente-trás) | Linear ascendente |
| Comida | Mão à boca repetido (comer) | Sagital | Linear repetitivo |
| Sim | Acenar para baixo (concordar com a cabeça, mas com a mão) | Vertical | Linear descendente |
| Não | Mão balançando lateralmente | Horizontal | Oscilação lateral |
| Banheiro | Mão descendo (gesto universal discreto) | Vertical | Linear descendente curto |
| Dor | Mão no local + apertar | Frontal | Pressão (pico de aceleração) |
| Socorro | Braço levantando rápido (chamar atenção) | Vertical | Linear ascendente forte |
| Obrigado | Mão saindo do peito (coração → fora) | Sagital | Linear para frente |

Cada gesto tem **plano diferente** e/ou **tipo de movimento diferente** — isso maximiza a distinção nos elementos orbitais.

### 14.3 Resultados reais — Testes com hardware (2026-03-29)

> **Nota para o IB:** Esta seção documenta os resultados REAIS obtidos ao implementar e testar o modelo orbital no hardware M5StickC Plus2. Os dados abaixo são de testes com o dispositivo físico, não simulações.

**Implementação completa no ESP32:**
- ✅ Extração de 8 features orbitais em tempo real (<1ms no ESP32 a 240MHz)
- ✅ PCA 3×3 por fórmula de Cardano (sem iteração, sem bibliotecas externas)
- ✅ Persistência no SPIFFS via JSON (campos sig_a, sig_b, sig_a_gyro)
- ✅ Fallback automático para sequence similarity (gestos treinados antes do modelo orbital)
- ✅ RAM: 80.7KB (24.6% de 327KB) | Flash: 2.3MB (87.8% de 2.6MB)

**Primeiro reconhecimento real:**
- Gesto: "água" (levar mão à boca)
- Confiança: **58%** (threshold: 55%)
- Áudio: tocou corretamente após match
- Tempo de processamento: imperceptível (<50ms do fim da gravação ao match)

**Assinatura orbital típica do gesto "água" (média de 3 amostras de treino):**

| Feature | Valor | Interpretação |
|---|---|---|
| amplitude | 0.67g | Gesto moderado |
| peak | 1.50g | Pico de aceleração |
| linearity | 0.97 | Quase totalmente linear (braço vai e volta) |
| plano normal | [-0.80, -0.30, 0.53] | Movimento predominante no eixo Y |
| duration | 1540ms | ~1.5 segundos de stroke |
| smoothness | 0.72 | Movimento fluido |
| rotation | 0.44 | Misto translação/rotação |

**Problemas encontrados e corrigidos nos testes:**

| Problema | Causa raiz | Solução |
|---|---|---|
| Stroke de 440ms em vez de 3000ms | Double-tap gera vibração que dispara onset falso | Cooldown 300ms + min stroke 1s + fallback buffer completo |
| Gestos rejeitados por movement insuficiente | Threshold 5.0 muito alto para gestos reais (3.4-4.0) | Reduzido para 3.0 |
| Score de 64% com threshold de 75% | Variação natural de orientação do sensor na mão | Threshold reduzido para 55%, peso do plano reduzido |
| Contexto "estou" disparado sem intenção | Bug pré-existente (trained=false) + qualquer movimento do Sensor B | Corrigido trained=false, filtro de movement pendente |

**Insight fundamental dos testes (para o IB):**

A assimetria temporal entre treino e reconhecimento é o desafio central. No treino, há um countdown de 3 segundos que permite ao usuário voltar ao repouso antes de gravar. No reconhecimento, a gravação começa imediatamente após o double-tap. Isso causa:
- Calibração de repouso contaminada pela vibração residual
- Stroke detection capturando a preparação em vez do gesto real
- Features extraídas de janelas temporais diferentes

A solução atual (fallback para buffer completo) funciona mas é sub-ótima. A solução ideal seria equalizar a janela temporal — ou adicionar um breve countdown no reconhecimento, ou melhorar a segmentação automática do stroke.

### 14.4 Próximos passos

1. **Testar discriminação** entre 2-3 gestos distintos (água vs sede vs não)
2. **Filtrar contextos falsos** — threshold de movement mais alto para Sensor B
3. **Melhorar score** — ajustar pesos por feature com base em dados reais
4. **Implementar composicionalidade** — "quero" (mão esquerda) + "água" (mão direita) = "Quero água"
5. **Testar persistência** — reiniciar e verificar se match funciona com signatures do SPIFFS

---

## 15. Fontes

### Biomecânica e motor control
- Bernstein, N.A. (1967). "The Co-ordination and Regulation of Movements." Pergamon Press.
- Vereijken, B. et al. (1992). ["Free(z)ing Degrees of Freedom in Skill Acquisition."](https://www.tandfonline.com/doi/abs/10.1080/00222895.1992.9941608) Journal of Motor Behavior, 24(1).
- PMC (2021). ["A Vexing Question in Motor Control: The Degrees of Freedom Problem."](https://pmc.ncbi.nlm.nih.gov/articles/PMC8801616/)
- Schmidt, R.A. & Lee, T.D. (2011). "Motor Control and Learning." Human Kinetics, 5ª ed.
- Fitts, P.M. & Posner, M.I. (1967). "Human Performance." Brooks/Cole.

### Linguística de sinais
- Stokoe, W.C. (1960). "Sign Language Structure." Gallaudet University Press.
- Brentari, D. (1998). "A Prosodic Model of Sign Language Phonology." MIT Press.
- PMC (2013). ["The Phonological Organization of Sign Languages."](https://pmc.ncbi.nlm.nih.gov/articles/PMC3608481/)
- Quadros, R.M. & Karnopp, L.B. (2004). "Língua de Sinais Brasileira." Artmed.
- [Handspeak — Parameters: handshape, location, movement, palm orientation](https://www.handspeak.com/learn/397/)

### Taxonomia de gestos
- Kendon, A. (2004). "Gesture: Visible Action as Utterance." Cambridge University Press.
- McNeill, D. (1992). "Hand and Mind: What Gestures Reveal about Thought." University of Chicago Press.
- [McNeill Lab — Gesture: A Psycholinguistic Approach](https://mcneilllab.uchicago.edu/pdfs/gesture.a_psycholinguistic_approach.cambridge.encyclop.pdf)

### Sistemas históricos
- Quintiliano. "Institutio Oratoria" (c. 95 d.C.), Livro XI.
- [Wikipedia — Chironomia](https://en.wikipedia.org/wiki/Chironomia)
- [ResearchGate — Cicero and Quintilian on hand gestures](https://www.researchgate.net/publication/231908988_Cicero_and_Quintilian_on_the_oratorical_use_of_hand_gestures)
- Bharata Muni. "Natya Shastra" (c. 200 a.C. — 200 d.C.)
- [Wikipedia — List of mudras (dance)](https://en.wikipedia.org/wiki/List_of_mudras_(dance))
- [Sahapedia — Mudra: Various Aspects and Dimensions](http://www.sahapedia.org/mudra-various-aspects-and-dimensions-0)
- [Wikipedia — Monastic sign languages](https://en.wikipedia.org/wiki/Monastic_sign_languages)
- [Medievalists.net — The Origins of Cistercian Sign Language](https://www.medievalists.net/2015/08/the-origins-of-cistercian-sign-language/)
- [Britannica — Plains Indian Sign Language](https://www.britannica.com/topic/Plains-Indian-sign-language)
- [AIATSIS — Yolŋu Sign Language](https://aiatsis.gov.au/blog/yolnu-sign-language)
- [U.S. Army TC 3-21.60 — Visual Signals](https://rdl.train.army.mil/catalog-ws/view/100.ATSC/0EE94275-C9B1-4C75-B9F3-CBB07D4251A4-1490024166167/tc3_21x60.pdf)

### Sistemas de movimento
- Laban, R. (1966). "Choreutics." Dance Books.
- [Wikipedia — Laban Movement Analysis](https://en.wikipedia.org/wiki/Laban_movement_analysis)
- [Springer — Laban movement analysis and HMMs for gesture recognition](https://jivp-eurasipjournals.springeropen.com/articles/10.1186/s13640-017-0202-5)
- [IJSTR — Dance Gesture Recognition Using LMA](https://www.ijstr.org/final-print/feb2020/Dance-Gesture-Recognition-Using-Space-Component-And-Effort-Component-Of-Laban-Movement-Analysis.pdf)
- [Britannica — Labanotation](https://www.britannica.com/art/labanotation)
- [Wikipedia — Conducting](https://en.wikipedia.org/wiki/Conducting)
- [Springer — A Grammar of Expressive Conducting Gestures](https://link.springer.com/chapter/10.1007/978-3-031-57892-2_5)
- Yang Chengfu. "The Essence of Tai Chi Chuan" (1934).

### Ciência cognitiva
- [Stanford Encyclopedia of Philosophy — Embodied Cognition](https://plato.stanford.edu/archives/fall2020/entries/embodied-cognition/)
- [Springer — Research Avenues Supporting Embodied Cognition (2024)](https://link.springer.com/article/10.1007/s10648-024-09847-4)
- [PMC — Proprioceptive loss and arm movements (2018)](https://pmc.ncbi.nlm.nih.gov/articles/PMC6061502/)

### Fadiga e acessibilidade
- [Springer — Dispelling the Gorilla Arm Syndrome (2017)](https://link.springer.com/chapter/10.1007/978-3-319-57987-0_41)
- [MPI — Modeling the Gorilla Arm Effect (2021)](https://people.mpi-inf.mpg.de/~ncheema/Arm_Fatigue_MIG_Poster_21.pdf)

### Fases do gesto
- [Bressem & Ladewig (2011) — Rethinking gesture phases](http://www.janabressem.de/wp-content/uploads/2016/10/Bressem-Ladewig-2011-gphases.pdf)
- [ScienceDirect — Gesture phase segmentation using SVMs (2016)](https://www.sciencedirect.com/science/article/abs/pii/S0957417416300525)
- [ArXiv — Co-Speech Gesture Detection through Multi-Phase Sequence Labeling](https://arxiv.org/html/2308.10680v2)

### Design de gestos
- Wobbrock, J.O. et al. (2009). ["User-Defined Gestures for Surface Computing."](https://faculty.washington.edu/wobbrock/pubs/chi-09.02.pdf) ACM CHI.
- [ACM — Memorability of pre-designed and user-defined gesture sets (2013)](https://dl.acm.org/doi/10.1145/2470654.2466142)

### Reconhecimento com IMU
- [IEEE — Real-Time Continuous Gesture Recognition with IMU](https://ieeexplore.ieee.org/document/8531095)
- [PLoS ONE — Multimodal hand gesture recognition using single IMU (2019)](https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0227039)
- [IEEE — Accelerometer-based gesture classification using PCA](https://ieeexplore.ieee.org/document/6064356)
- [ResearchGate — 6DOF IMU Head Gesture Detection: Jerk-Based Feature Extraction](https://www.researchgate.net/publication/346682461_6DOF_Inertial_IMU_Head_Gesture_Detection_Performance_Analysis_Using_Fourier_Transform_and_Jerk-Based_Feature_Extraction)
- [PMC — MGRA: Motion Gesture Recognition via Accelerometer](https://pmc.ncbi.nlm.nih.gov/articles/PMC4851044/)
- [Microsoft Research — Direct Least Square Fitting of Ellipses](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/ellipse-pami.pdf)

### Gestos cross-cultural
- [Britannica — Is Body Language Universal?](https://www.britannica.com/story/is-body-language-universal)
- [Penn State — Influence of cultural factors on gesture design](https://www.psu.edu/news/research/story/researchers-study-influence-cultural-factors-gesture-design)
- [Matsumoto & Hwang — Cultural Similarities and Differences in Emblematic Gestures](http://www.davidmatsumoto.com/content/2013%20M%20and%20H%20JNB%20-%20Cultural%20Similarities%20and%20Differences%20in%20Emblematic%20Gestures.pdf)

---

*Documento gerado em 2026-03-29 para o projeto GESTUUM — Science Fair COREE 2026 (IB).*
*Base teórica para a inovação no motor de reconhecimento de gestos.*
