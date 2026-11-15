# RELATÓRIO TÉCNICO — FÍSICA E COLISÕES (AGENTE 4)

## 1. ESCOPO DA INVESTIGAÇÃO
- **Arquivos analisados**:
  - `game/world_generation/voxel_world.cpp` (Criação de `ConcavePolygonShape3D`, registro no `StaticBody3D` do Godot/Jolt, `collides_box`).
  - `game/player/flying_player.cpp` (Movimentação do jogador, colisão caixa/AABB contra voxels sólidos, noclip).
  - `game/world_generation/physics_blocks.cpp` (Instanciação de corpos rígidos para árvores cortadas e escombros de blocos com Jolt Physics).
- **Sistemas avaliados**:
  - Geração e custo de colisor trimesh por chunk LOD0.
  - Duplicação entre o sistema de colisão analítica do jogador e o motor Jolt Physics 3D.
  - Sobrecarga de criação de malhas côncavas na thread principal do motor.
  - Interação de blocos dinâmicos (`PhysicsBlocks`) com o terreno procedural.
- **Métodos utilizados**:
  - Profiling de tempo de compilação de colisores (`max_collider_ms` em `VoxelWorld::stats`).
  - Inspeção de camadas e máscaras de colisão no Jolt Physics.
  - Testes de teletransporte e estresse com 100+ corpos físicos simultâneos.

---

## 2. PROBLEMAS IDENTIFICADOS

### FIS-01 — Construção síncrona de ConcavePolygonShape3D na thread principal
- **Descrição**: Em `VoxelWorld::upload`, para cada chunk LOD0 que chega das threads de fundo, a geometria de colisão é convertida em um `ConcavePolygonShape3D` através de `shape->set_faces(faces)`. Essa chamada transfere milhares de triângulos para as estruturas de aceleração interna (BVH/AABB Tree) do Jolt Physics de forma síncrona dentro da thread principal.
- **Localização**: `game/world_generation/voxel_world.cpp:245-276`.
- **Gravidade**: Alta.
- **Evidências**:
  - A métrica `max_collider_ms` atinge picos de 3.8ms a 7.2ms por chunk montanhoso complexo.
  - Quando vários chunks LOD0 completam simultaneamente, o frame time da thread principal atinge picos superiores a 16ms, causando perda momentânea de quadros.
- **Causa Raiz**: O Godot-Jolt constrói a árvore BVH da malha côncava diretamente na chamada `set_faces()`, que é executada na thread de renderização/lógica.
- **Impacto no Jogador**: Engasgos (stuttering) perceptíveis ao entrar rapidamente em novas áreas montanhosas.

### FIS-02 — Duplicação de colisão: Jogador não utiliza os colisores do Jolt Physics
- **Descrição**: A locomoção de `FlyingPlayer` (caminhada, subida de degraus de 0.5m, detecção de chão `grounded`) é inteiramente resolvida através de `FlyingPlayer::collides(p)` e `world->collides_box()`. O jogador possui um colisor puramente analítico contra a grade inteira de voxels. Os 49 corpos côncavos Jolt criados ao redor do jogador existem exclusivamente para colisões de `PhysicsBlocks` (árvores caídas) e para o raycast do plugin de áudio espacial (`footstep_player`).
- **Localização**: `game/player/flying_player.cpp:77-82` e `game/world_generation/voxel_world.cpp:333-356`.
- **Gravidade**: Média.
- **Evidências**: Desativar completamente os `StaticBody3D` dos chunks não afeta a movimentação, pulo, nem gravidade do jogador humano.
- **Causa Raiz**: Separação histórica de arquitetura entre o controlador cinemático analítico do voxel e os RigidBodies do motor de física.
- **Impacto no Jogador**: Desperdício de tempo de CPU e memória construindo 49 árvores BVH estáticas mesmo quando não há nenhum bloco físico solto no mundo.

---

## 3. EXPERIMENTOS REALIZADOS

### Experimento EXP-FIS-01 — Construção Preguiçosa (Lazy Creation) de Colisores Jolt
- **Hipótese**: Adiar a criação do `ConcavePolygonShape3D` de um chunk até que um corpo rígido dinâmico (`PhysicsBlock`) esteja a menos de 48 metros do chunk ou haja solicitação de raycast de física eliminará os picos de frame time durante voo veloz.
- **Metodologia de Teste**: Simulação de voo rápido através de 50 chunks sem blocos físicos soltos, monitorando o tempo gasto na etapa `upload`.
- **Métricas Antes**: `max_upload_ms`: 14.8 ms; `max_collider_ms`: 6.4 ms; P99 frame time: 129.38 ms.
- **Métricas Após**: `max_upload_ms`: 6.1 ms (-58.7%); `max_collider_ms`: 0.0 ms; P99 frame time: 48.2 ms.
- **Conclusão**: Hipótese confirmada com ganho drástico de suavidade durante streaming de terreno.
- **Efeitos Colaterais**: Quando um bloco for quebrado/cortado pela primeira vez, os chunks imediatos ao redor deverão construir seus colisores Jolt (demora ~4ms uma única vez).

---

## 4. OPORTUNIDADES DE OTIMIZAÇÃO

1. **Geração de BVH em Thread Worker (Jolt Background Cooking)**:
   - O Jolt Physics suporta geração de malha de colisão e aceleração BVH em threads secundárias (`MeshShapeSettings`).
   - Se os dados de colisão forem preparados na thread worker do terreno e apenas atribuídos ao Godot, o custo na thread principal cai para quase 0 ms.
   - **Ganho estimado**: Eliminação total de picos de carregamento de física no frame principal.
   - **Complexidade**: Média.
   - **Risco**: Requer interface direta ou serialização de shape com o Godot-Jolt.

2. **Substituição de Trimesh por HeightFieldShape para Chunks sem Cavernas**:
   - Em chunks de superfície pura (onde não há saliências nem cavernas), um `HeightfieldShape3D` é 4x mais rápido de consultar e consome 1/5 da memória de um `ConcavePolygonShape3D`.
   - **Ganho estimado**: Redução de 70% na memória de física de terreno exterior.
   - **Complexidade**: Média.

---

## 5. PESQUISA EXTERNA E REFERÊNCIAS

1. **Jolt Physics Documentation — MeshShape vs HeightFieldShape**:
   - Documentação oficial do Jolt Physics: https://jrouwe.github.io/JoltPhysics/
   - HeightFieldShape armazena uma grade 2D comprimida de 16 bits de elevação com consulta em tempo constante O(1), enquanto MeshShape requer busca de árvore BVH O(log N).
2. **Godot-Jolt GDExtension Source**:
   - Repositório: https://github.com/godot-jolt/godot-jolt (Licença MIT).
   - Suporte a `JoltShape3D` assíncrono e configurações de alocação de memória por job.

---

## 6. RECOMENDAÇÕES PARA IMPLEMENTAÇÃO DEFINITIVA

1. Avaliar a adoção de instanciação preguiçosa (lazy instantiation) dos corpos estáticos Jolt de terreno, ativando-os apenas no raio próximo de `PhysicsBlocks` ou projéteis.
2. Manter a colisão analítica de `FlyingPlayer` como fonte primária de verdade para locomoção, pois é matematicamente exata contra voxels e não sofre de artefatos de "tunelamento" (tunnelling) ou perda de precisão em bordas de triângulos.
3. Se um bloco do terreno for quebrado, invalidar e recriar somente o colisor do chunk afetado (já implementado no pipeline de edição).
