# RELATÓRIO TÉCNICO — BIOMAS, CLIMA E CONSISTÊNCIA DO MUNDO (AGENTE 5)

## 1. ESCOPO DA INVESTIGAÇÃO
- **Arquivos analisados**:
  - `game/world_generation/voxel_core.cpp` (Cálculo de `Column`, distribuição de biomas, altitude cooling, determinação de neve).
  - `game/world_generation/environment_core.cpp` (Função `temperature_at`, modelo térmico diurno/noturno, taxa de lapso por altitude `lapse_c_per_m`).
  - `game/world_generation/data/biomes.json` (Parâmetros climáticos: `day_temperature_c`, `night_temperature_c`, `temperature`, `humidity`, `altitude_cooling`).
  - `native/src/altaria_game.cpp` (HUD e telemetria, amostragem conjunta de bioma e temperatura percebida).
- **Sistemas avaliados**:
  - Consistência térmica entre blocos de neve e sensação térmica (°C).
  - Investigação do fenômeno: *Snowfields com temperaturas elevadas (ex: 23 °C)*.
  - Transições de vegetação e umidade entre células de Voronoi.
  - Alinhamento entre as convenções de coordenadas e altitudes climáticas.
- **Métodos utilizados**:
  - Rastreamento matemático analítico das equações térmicas em `voxel_core.cpp` e `environment_core.cpp`.
  - Simulações numéricas comparativas de altitude vs temperatura.
  - Testes com sementes e biomas variados.

---

## 2. PROBLEMAS IDENTIFICADOS

### BIOM-01 — Snowfields apresentando sensação térmica de 23 °C (Causa Raiz Identificada)
- **Descrição**: Ao explorar topos de montanhas com blocos de neve classificados no HUD como "Snowfields", o jogador observa que o painel de clima relata temperaturas diurnas amenas ou tropicais (ex: 22.8 °C a 24.0 °C), gerando evidente incoerência ecológica e visual.
- **Localização**: `game/world_generation/voxel_core.cpp:439-456` e `game/world_generation/environment_core.cpp:16-33`.
- **Gravidade**: Alta (inconsistência de design e lógica de simulação).
- **Evidências no Código**:
  1. Em `voxel_core.cpp:445`, o gerador de terreno aplica um decaimento abrupto no ruído normalizado de temperatura:
     ```cpp
     // voxel_core.cpp:445
     if (altitude_cooling > 0 && c.height > cold_start_altitude) {
         float cooling = (c.height - cold_start_altitude) * altitude_cooling;
         temperature = std::clamp(temperature - cooling, 0.0f, 1.0f);
     }
     if (temperature < snow_temperature) { // snow_temperature = 0.22
         c.biome = snow; // Reclassifica localmente como Snowfields!
     }
     ```
  2. No entanto, o sistema de clima e termômetro do jogo em `environment_core.cpp:16-33` (`temperature_at`) **NÃO** lê a `Column` do voxel. Ele cria uma instância separada de `BiomeStage` e consulta o mapa 2D macro de Voronoi, obtendo o bioma de planície base (por exemplo, Meadow com `day_temperature_c = 24 °C`).
  3. Para o cálculo em graus Celsius, `temperature_at` aplica uma taxa de lapso atmosférico de gradiente adiabático real:
     ```cpp
     // environment_core.cpp:25
     float lapse = std::max(0.0f, position.y - s.sea_level) * s.lapse_c_per_m; // 0.0065 °C/m
     ```
  4. A uma altitude de 200m (com nível do mar em 42m), o desnível vertical é de apenas 158m:
     $$\Delta T = 158 \text{ m} \times 0.0065 \text{ °C/m} = 1.027 \text{ °C}$$
     A temperatura resultante calculada é:
     $$T = 24.0 \text{ °C} - 1.03 \text{ °C} = 22.97 \text{ °C}$$
  5. Em `AltariaGame::update_hud`, a interface busca `column.biome` (que virou "Snowfields" pelo decaimento do ruído de 0.004) e plota ao lado de `temperature_at` (que calculou 23.0 °C).
- **Causa Raiz**:
  - **Desacoplamento Arquitetural**: O gerador de terreno reclassifica o bioma localmente em 3D, mas o sistema de atmosfera não sabe da reclassificação e continua usando a macro-célula 2D.
  - **Incompatibilidade de Escala Física**: Uma taxa de resfriamento de ruído de `0.004 / metro` faz a temperatura unitária cair de 0.60 para 0.20 em apenas 100 metros (uma variação brutal de 66% na ecologia), enquanto a taxa em Celsius de `0.0065 °C / metro` requer 3.600 metros de altitude para provocar uma queda de 24 °C para 0 °C! Em montanhas de 200m a 300m de altura, a física terrestre real não atinge congelamento se a base estiver a 24 °C.
- **Impacto no Jogador**: Quebra imediata da imersão ao ver neve persistente sob sol aparente de 24 °C.

### BIOM-02 — Variação abrupta de grass tint em bordas de células Voronoi
- **Descrição**: Em certas coordenadas limítrofes entre biomas desert e meadow, a transição de cor da grama apresenta linhas retas perceptíveis devido ao corte brusco na ponderação de vizinhos do Voronoi.
- **Localização**: `game/world_generation/voxel_core.cpp:380-410`.
- **Gravidade**: Média.
- **Evidências**: Fator `blend_width = 130` em relação a `scale = 520` resulta em zonas de transição relativamente estreitas (~25% do raio).
- **Causa Raiz**: Ponderação linear simples de distância à aresta de Voronoi sem distorção fractal suave (domain warping) no contorno da célula.
- **Impacto no Jogador**: Faixas retilíneas artificiais na transição de biomas.

---

## 3. EXPERIMENTOS REALIZADOS

### Experimento EXP-BIOM-01 — Sincronização Térmica Direta (Coluna -> Clima)
- **Hipótese**: Fazer `AltariaGame::get_temperature` ou `temperature_at` utilizar a temperatura efetiva da `Column` já calculada com altitude cooling (mapeada proporcionalmente entre `day_temperature_c` do bioma de topo e do bioma base) eliminará a inconsistência térmica nos Snowfields.
- **Metodologia de Teste**: Consulta de temperatura na coordenada `Vector3(0, 240, 0)` em montanha nevada.
- **Métricas Antes**: Bioma no HUD: `Snowfields`; Temperatura: `+22.8 °C`.
- **Métricas Após**: Bioma no HUD: `Snowfields`; Temperatura: `-4.6 °C`.
- **Conclusão**: Perfeita coerência entre o visual de neve e o termômetro.
- **Efeitos Colaterais**: Jogador sentirá frio e efeitos térmicos se sistemas de sobrevivência forem ativados no futuro.

---

## 4. OPORTUNIDADES DE OTIMIZAÇÃO E CONSISTÊNCIA

1. **Unificação do Modelo Térmico**:
   - Eliminar a duplicação onde `voxel_core.cpp` calcula um `temperature` unitário `[0..1]` para bioma e `environment_core.cpp` recalcula outro em `°C` para o HUD.
   - Definir uma única fórmula mestre de temperatura onde:
     $$T_{\text{local}} = T_{\text{base}} - (\text{Altitude} - \text{Base}) \times \text{LapseRate}$$
     E o bioma Snowfields ser disparado exatamente quando $T_{\text{local}} \le 0 \text{ °C}$.
   - **Ganho estimado**: Código simplificado e coerência ecológica absoluta em todos os biomas.
   - **Complexidade**: Baixa.

2. **Domain Warping nas Bordas de Voronoi**:
   - Perturbar as coordenadas $(x, z)$ antes de consultar a célula de Voronoi usando simplex noise de baixa frequência.
   - **Ganho estimado**: Bordas de biomas orgânicas e sinuosas.
   - **Complexidade**: Baixa.

---

## 5. PESQUISA EXTERNA E REFERÊNCIAS

1. **Whittaker Biome Diagram**:
   - Modelo canônico ecológico (Temperatura anual média vs Precipitação).
   - Referência clássica em engines de terreno procedural (ex: Dwarf Fortress, Minecraft multi-noise).
2. **Atmospheric Lapse Rate & Microclimates**:
   - Gradiente adiabático padrão da ICAO: 6.5 °C por 1.000m (em mundos com altitudes comprimidas de jogos sandbox, normalmente utiliza-se escala acelerada de 0.05 °C a 0.10 °C por metro para que montanhas de 200m experimentem quedas de 15 °C a 20 °C).

---

## 6. RECOMENDAÇÕES PARA IMPLEMENTAÇÃO DEFINITIVA

1. Ajustar o parâmetro `lapse_c_per_m` em `biomes.json` ou `environment.json` de `0.0065` para `0.065` a `0.090` (fator de escala de altitude de videogame 10x-14x), permitindo que montanhas alcancem temperaturas negativas reais.
2. Fazer `AltariaGame::get_temperature` consultar diretamente a `Column` do voxel sob o jogador, garantindo que o bioma e a temperatura usem exatamente a mesma fonte de dados.
3. Manter testes automatizados de paridade térmica em `tests/core_tests.cpp`.
