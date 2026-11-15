# Streaming dinâmico do Altaria

## Reconhecimento e manifesto

O ponto de partida foi `eff109c` com alterações locais de cavernas e superfície. O binário debug anterior foi preservado em uma fixture. Seu SHA-256 é `abe57427519a338dac78ac40b9917463694967ecd85a9591e5ccb70e25517dc4`.

- Godot 4.7.1 do GDstudio, Vulkan 1.4.329 Forward+, RTX 4050 Laptop e i7-13620H. Geração C++ e FastNoise2, colisão Jolt criada sob demanda.
- Seed 1337, hora 10 pausada, 1280×720 com SubViewport do perfil padrão, VSync desligado. Fixture descartável criada com `tests/create_cave_fixture.py <diretório> reform`. A fixture retira autoloads opcionais de terceiros. O Lens Effects integra a cena em Vulkan.
- `tests/dynamic_streaming_runtime.gd` usa caminhada simulada de 5 m/s por 3 s, voo de 85 m/s por 5 s, 2 s com fog de deserto em transição e 2 s parado em uma câmara abissal aberta, próxima de (117.86, −262.30, 187.47). A câmera leva uma luz local para a câmara. As posições são controladas pelo teste e a física de locomoção fica desativada nesse percurso.
- Perfis comparados com a mesma configuração de geração. O primeiro usa o binário e shaders anteriores. O segundo ativa a seleção adaptativa e o recorte vertical, com os shaders anteriores. O terceiro reutiliza o pipeline de iluminação, pula contornos quando o fog é opaco e corta a luz externa em cavernas fechadas.
- Para repetir, defina `ALTARIA_STREAM_OUTPUT` para um diretório descartável, `ALTARIA_STREAM_CAP=0` e `ALTARIA_STREAM_REAL_CAVE=1`. Execute `gdstudio --disable-crash-handler --rendering-driver vulkan --resolution 1280x720 --path <fixture> --script res://tests/dynamic_streaming_runtime.gd`. `ALTARIA_STREAM_CAPTURE=1` grava as capturas. Use diretórios XDG descartáveis.
- Frame times medidos entre frames pelo relógio do Godot. Memória de voxels em bytes de BlockId dos volumes residentes. RSS é o pico do processo medido por `/usr/bin/time -v`. Sem medida de tempo individual de GPU e VRAM.

## Descobertas e decisões

- `cave_runtime_audit`, agente Codex nativo somente leitura, identificou fila FIFO com trabalhos obsoletos e recriação do pipeline de computação dos raios solares a cada atualização de uniforms. A etapa de investigação produziu evidências em código. A revisão adicional desse agente encerrou por limite de uso.
- `cave_architecture_audit`, agente Codex nativo somente leitura, mapeou o contrato XYZ e revisou o patch. A revisão apontou saias artificiais no corte, faces de slabs, dados antigos na fila, reativação de líquidos e um problema de alcance para blocos Jolt. O coordenador corrigiu esses pontos e executou os testes. Não houve confirmação de um modelo diferente para revisão.
- O coordenador foi o único autor das alterações. Mantive o fallback `adaptive_streaming: false` para comparação. Os testes antigos de perfil legado continuam usando sua configuração explícita, e o teste novo exercita o perfil ativo.
- A seleção horizontal conserva a cobertura de LOD até o fim opaco do fog, com pelo menos 128 m de folga, e estende a zona detalhada na direção do deslocamento. Trabalhos obsoletos na fila são cancelados antes da geração.
- A geração de superfície inclui a boca e o trecho raso das cavernas. Abaixo de cobertura suficiente, o streamer trabalha com um raio local de 256 m e aproximadamente 192 m de altura, ajustável pela demanda de corpos ativos. Faixas omitidas têm disponibilidade explícita. Edições de sessão seguem no modelo esparso.
- As capturas do primeiro candidato revelaram que a remoção da superfície também removia seu bloqueio da luz direcional. A versão final suspende sol, lua e raios solares enquanto o modo subterrâneo está ativo. As luzes locais seguem ativas.
- A seleção vertical usa relevo amostrado e sondas ascendentes em voxels residentes. A triagem é conservadora perto da superfície e é aproximada em geometrias incomuns. Planejamento procedural de redes pode continuar alimentando o cache mesmo quando seus volumes e meshes profundos estão fora da faixa ativa.
- A redução de detalhe faz as copas passarem para LODs mais simples mais cedo. Mantive o horizonte voxel visível através do fog. A pré-geração em tela de loading não foi necessária no perfil medido.

## Resultados

Mediana das três repetições Vulkan sem teto de FPS. O perfil adaptativo isolado teve duas repetições. O perfil intermediário com shaders atualizados, antes da correção de luz externa, teve uma repetição e está identificado como `final` no CSV. O código entregue corresponde a `checked`.

| Cenário | Antes P50 / P99 | Adaptativo P50 / P99 | Entregue P50 / P99 |
| --- | ---: | ---: | ---: |
| Deslocamento lento | 3,11 / 3,75 ms | 1,45 / 2,34 ms | 1,45 / 2,64 ms |
| Voo rápido | 2,58 / 5,61 ms | 1,63 / 3,42 ms | 1,62 / 3,57 ms |
| Fog mais curto | 3,55 / 4,72 ms | 1,50 / 2,37 ms | 1,49 / 2,24 ms |
| Câmara abissal | 3,41 / 4,51 ms | 0,91 / 1,56 ms | 0,57 / 1,26 ms |

| Métrica | Antes | Entregue |
| --- | ---: | ---: |
| Carga inicial do terreno | 1,50 s | 0,55 s |
| Tiles residentes ao iniciar | 444 | 260 |
| LOD0 ao iniciar | 44 | 24 |
| Buffer de voxels ao iniciar | 32,99 MB | 5,19 MB |
| Tiles na câmara | 461 | 104 |
| Triângulos residentes na câmara | 2.581.308 | 156.598 |
| Pico RSS mediano por processo | 1.185 MiB | 1.045 MiB |
| Pior frame dos três voos | 9,47 ms | 19,99 ms |

O ganho principal veio da redução da cobertura detalhada, do corte pelo fog e das faixas verticais. O teste intermediário com shaders atualizados indicou melhora pequena ou variável fora da caverna. O pipeline agora é mantido entre atualizações de uniforms. Na câmara, a remoção da luz externa reduz trabalho adicional e repõe a oclusão observada na captura de referência.

O P99 de voo melhorou, com um pico localizado entre 14,95 e 19,99 ms em cada repetição final por volta de 2,3 s de percurso. Duas das três repetições tiveram um frame acima de 16,67 ms, contra zero antes. O maior upload de mesh medido até o fim do voo subiu para 12,59–17,41 ms. A causa do pico do frame ainda precisa de atribuição por tile e pela fila gráfica. A série exploratória com teto de 120 FPS ficou perto de 8,3 ms na mediana e ocultou boa parte do ganho de throughput.

O teste de entrada na câmara inclui 700 ms de espera antes da checagem de assentamento. Ele valida cobertura, mas não mede com precisão a latência mínima de teleporte.

## Verificação

- Build nativo concluído. Suíte C++ com 231.423 verificações, além de sessões efêmeras e hidrologia Jolt, passou.
- Teste de bandas verificou que um salto apenas em Y disponibiliza a faixa inferior, libera a faixa anterior e mantém uma edição ao voltar.
- O teste Vulkan validou câmara navegável, edição de superfície após descida e retorno e um corpo Jolt que solicita o collider fora do raio de detalhe ocioso.
- As capturas de superfície e câmara foram inspecionadas. A silhueta distante continua presente. A vegetação simplifica mais cedo. A câmara mantém luz local e iluminação ambiente, com o sol bloqueado.
- O teste não cobre travessia contínua por todos os poços e cavernas, oclusão de cada geometria do mundo, todas as seeds ou movimento com entrada real no limite máximo de sprint. O problema CAV-014 continua aberto para os picos de upload no percurso de voo.

Dados por execução em [metrics.csv](raw/metrics.csv). Amostras brutas, logs e RSS em [repetitions.tar.gz](raw/repetitions.tar.gz).
