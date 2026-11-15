# Relatório da operação
## Identificação
ID, data/fuso, pedido, escopo, coordenador, revisão/hashes e estado.
## Reconhecimento
Engine, dependências, sistemas/testes, hardware, renderer/driver, bugs conhecidos, limitações.
## Benchmark inicial
Manifesto reproduzível, seeds/save/cena/percurso, warmup/repetições, comandos, amostras/logs, unidades e métricas indisponíveis.
## Descobertas por agente
IDs reais, provedores/modelos confirmados, tarefa, problema, evidências, arquivos, impacto, hipótese causal, recomendação, risco e teste.
## Plano de otimização
Prioridade e justificativa, impacto/frequência/gravidade/risco/complexidade/confiança; dependências, arquivos reservados e responsáveis.
## Registro de alterações
Diffs, autoria, integração sequencial e rollback.
## Resultados dos testes
CAÇADOR e JUIZ, IDs, execução, logs, regressões, visual, decisão independente.
## Benchmark final
Mesmo manifesto, resultados por execução, variabilidade, deltas e decisão. Nunca preencher números inventados.
## Problemas pendentes
IDs no histórico, próximos passos, bloqueios, fases não executadas e motivos.

## Reconhecimento e plano
A base Git é eff109c com alterações locais, incluindo a entrega de cavernas e
trabalho paralelo de assets, player e console. Operação anterior documenta
GDstudio 4.7.1 / Forward+ / RTX 4050; esta operação mede novamente. Os três
registros da seed 1337 mostram água no fundo de cortes abruptos e faces altas
com alternância de materiais. A base numérica registra três janelas e uma
amostra regional em três seeds. Testes descartáveis, sem saves.

O coordenador é o único escritor. cave_architecture_audit e cave_runtime_audit
foram acionados para leitura, com modelo herdado sem identificação exposta.
Áreas reservadas: planner/configuração das cavernas, perfil superficial,
registro/render de blocos e validação. Preservar geometria editável, cache,
emendas dos LODs, hidrologia subterrânea e construção. Medir profundidade real,
entradas acessíveis, distribuição superficial e FPS antes da entrega.

## Etapa 1 — Cavernas

A primeira etapa foi dedicada às cavernas. Rios, litoral e texturas continuam como próximas
etapas. A auditoria de superfície anterior à priorização está em
`raw/surface-baseline.log`. Na seed 1337, as janelas de captura registraram até
43,23 m de diferença entre amostras adjacentes. O especialista atribuiu o corte
local à puxada do canal até o nível do mar, com fronteira de bioma inativa nesse
corredor. Os pilares da segunda imagem ainda precisam de diagnóstico.

### Alterações

- Piso em −896, profundidades alvo 48/168/405/690 m, corredores entre níveis
  com curvas e salões grandes reservados no nível abissal.
- Poços, ravinas e bocas com declive selecionado. A entrada é publicada
  depois de conectar o acesso local ao salão abissal. O acesso exterior usa
  uma abertura curta e uma junção explícita com o corredor enterrado.
- Variação de biomas subterrâneos desligada nos dados ativos e habilitável
  com `biome_variation`. O critério por cobertura vertical também pode trocar
  materiais de rocha profunda expostos em faces de cortes superficiais.
- LOD0–2 compactam rocha sólida uniforme e céu fora da faixa útil. As áreas
  com cavidades, árvores e edições continuam materializadas. Rocha e bedrock
  continuam sólidos abaixo dessa faixa, e o piso conserva sua face inferior.
- O comando `cave entrances` lista entradas da região. O `tp` e o voo usam
  o piso do mundo carregado.

### Verificação

CAÇADOR `/root/cave_runtime_audit` executou o teste anterior e a primeira
versão do teste novo. Identificou bocas sem declive e a junção implícita.
JUIZ `/root/cave_architecture_audit` verificou limites, substrato, materiais
e custo. A integração corrigiu a junção, a validação do declive, o limite das
máscaras de formação e o collider da face inferior de bedrock. Os agentes
usaram o modelo herdado, sem identificador próprio exposto.

- O piloto verificou 43 entradas. O perfil final, com controle de inundação,
  verificou 26 entradas secas em três seeds, com 12 poços, 2 bocas e 12
  ravinas. As rotas de voxels com dois blocos de altura chegam a um salão
  gigante. A menor coordenada de câmara amostrada foi −679.
- LOD0–2 testados em 14.896 amostras de fronteira para as janelas compactas,
  além de edições abaixo do substrato e acima do teto anterior do chunk.
- No Godot, seed 1337, o voo permaneceu em Y = −679, cerca de 698 m abaixo
  do relevo local. A edição em Y = −880 sobreviveu à reconstrução e o bloco
  vizinho continuou sólido. O teste usou o perfil real com hidrologia.

### Tempos de quadro

GDstudio 4.7.1 Forward+ Vulkan 1.4.329, RTX 4050 Laptop, 1280×720,
vsync desligado, cap 120 FPS. Seed 1337, cena principal, relógio pausado,
espera por fila vazia. Dois segundos estacionário, quatro segundos de voo
a (85, 0, 20) m/s, a partir de (100, 135, 100). Projeto e diretórios XDG
descartáveis. Duas rodadas antigas e três após a compactação.

| Perfil | Pronto (ms) | Buffers estáveis de voxels (bytes) | P99 em voo (ms) | Máximo em voo (ms) |
| --- | ---: | ---: | ---: | ---: |
| Anterior | 5590–5720 | 78.330.560 | 14,23–15,40 | 18,33–25,93 |
| Cavernas compactas | 3980–4087 | 36.887.960 | 16,66–16,90 | 19,76–25,84 |

Os buffers medidos são os dos voxels residentes. O RSS inclui o renderer,
outros dados e cenários extras de inspeção na primeira rodada nova.
O P99 estacionário variou entre 8,73 e 8,80 ms antes e 8,69 e 8,81 ms
depois. Houve um outlier estacionário de 304 ms em uma rodada anterior.
A compactação reduziu os buffers em 53%. O P99 em voo piorou entre os
perfis e os picos durante o streaming continuam em aberto. CPU e VRAM por
frame não foram coletadas. As amostras, logs e o script estão em `raw/`.

### Hidrologia, forma dos salões e rodada final

A inspeção visual do primeiro perfil compacto encontrou oito salões submersos
em cinco bacias. Uma única entrada molhada propagava a lâmina de água a todo o
conjunto. O perfil final limita as bacias que podem abrir para água superficial,
procura entradas secas em direções alternativas e escolhe lagos no nível mais
fundo. A amostra nativa verificou 85 de 94 salões gigantes secos. A revisão
independente de `/root/cave_runtime_audit` conferiu a cadeia de callbacks e o
limite de inundação quando o arquétipo opcional está ausente.

A forma dos salões recebeu colunas de rocha, saliências e pináculos modelados
no campo de voxels. As passagens preservam espaço na interseção com as colunas.
A captura `raw/dry-hall.png` usa uma luz temporária do teste para inspeção.
A última rodada renderizada validou voo e mineração em profundidade e registrou
4.648 ms para ficar pronta, 36.887.960 bytes nos buffers de voxels estáveis e
P99 de 22,47 ms durante o voo. As duas rodadas anteriores, com a hidrologia
final e antes das últimas formas rochosas, registraram P99 de 19,58 e 16,80 ms;
uma teve um pico de 253,62 ms. A origem desse pico segue sem atribuição.

## Etapa 2 — Superfície e texturas

A etapa anterior concluiu as cavernas e forneceu a base para comparar a
superfície em projetos descartáveis. O relatório externo forneceu contexto
sobre os vales, as camadas de rocha e a vegetação já presentes no projeto.

### Causas confirmadas na seed 1337

- O auditor `/root/cave_architecture_audit` comparou 24.800 arestas entre
  colunas na janela x=−128…0, z=128…224. A puxada do canal elevava a maior
  diferença local de 7,56 m para 43,23 m e criava 911 arestas acima de 10 m.
  As fronteiras de bioma não contribuíam para aquele corte.
- Na coluna (−75,142), a regra antiga do arco somava cerca de 9 m, sem um
  ganho equivalente nas quatro vizinhas. O ponto estava no LOD0. As formas
  altas e estreitas vinham do gerador de superfície.
- O auditor `/root/cave_runtime_audit` confirmou que a grama nova é um
  sprite com alpha já consumido pelo material de vegetação, que as folhas
  precisam de recorte próprio e que o material anterior usava uma camada
  única para cada tronco. Ambos os especialistas entregaram os achados
  antes de atingirem o limite de uso do ambiente.

### Alterações e verificação

- A versão 3 gera continentes em escala maior, costa inclinada, cordilheiras
  apoiadas em colinas e vales fluviais que distribuem o desnível antes de
  cortar o canal. O leito final remove no máximo 5,5 m. O perfil descarta o
  aumento abrupto de altura dos arcos e as faixas de rocha repetidas por Y.
- Praias ocupam um intervalo estreito de altitude e aparecem em costas
  de relevo baixo. Cascalho aparece em fundos rasos e leitos de rios, com
  rocha exposta em trechos íngremes.
- A nova textura da casca e o corte do tronco são resolvidos pela normal
  local na malha estática, nas formas e nos blocos de física. O recorte
  das folhas usa o alpha do PNG com metadado independente da exclusão da
  outline. A textura opaca do bloco de grama e o sprite `grass.png` mantêm
  as funções específicas. O catálogo ativo deixa de carregar as três
  decorações de pedrinhas e os mapas fornecidos de minério e pebble
  ficam disponíveis para a etapa futura.
- A suíte C++ passou 231.447 verificações. As três janelas de 65×65 colunas
  da seed 1337 e o caso de regressão da região mostram saltos de no máximo
  4,80 m no novo perfil, frente a 43,23 m no anterior. As capturas
  `raw/surface/{before,after}-old-*-site.png` usam os mesmos pontos de vista.
- A amostra de 341.056 colunas por seed (1337, 42 e −89, passo de 48 m)
  registrou 98.628 colunas de canal molhado forte antes e 9.481 depois.
  A máscara de praia acima de 0,18 caiu de 78.584 para 6.308 colunas.
  A maior sequência oceânica ao longo de X passou de 2,35–3,02 km para
  6,53–7,87 km na malha amostral. As contagens de picos acima de 145 m
  ficaram na mesma ordem de grandeza. Fonte e resultados estão em
  `raw/surface/terrain-probe.cpp` e `probe-*.log`.
- O teste nativo no novo perfil validou 36 rotas de entrada até salões
  gigantes em três seeds, incluindo bocas, poços e ravinas, e verificou 85
  salões secos entre 94. A câmara mais profunda amostrada ficou em Y = −708.
  O teste de integração na seed 1337 chegou a uma câmara seca em Y = −659,
  694,5 m abaixo do relevo local. Voar e minerar abaixo da faixa compactada
  continuou funcionando após reconstruir o chunk.
- O teste visual no Godot conferiu vale, costa sem areia contínua, cascalho,
  madeira, folhas e conversão de um tronco em corpo físico. A cena final
  carregou os seis LODs e zero pedrinhas. A cena anterior contava 12.531.

### Streaming no Godot

GDstudio 4.7.1, Vulkan Forward+ e RTX 4050 Laptop, 1280×720, cap de 120 FPS
e vsync desligado. Seed 1337. Depois da fila esvaziar, 2 s estacionário e
4 s a (85,0,20) m/s com origem (100,200,100). Duas repetições por perfil.
A base já inclui a reforma das cavernas e usa o mesmo percurso da superfície.

| Perfil | Pronto (ms) | Buffers residentes de voxels (bytes) | P99 em voo (ms) | Máximo em voo (ms) |
| --- | ---: | ---: | ---: | ---: |
| Superfície anterior | 3943–4084 | 36.887.960 | 17,16–18,04 | 20,99–21,95 |
| Superfície final | 3813–3880 | 35.581.680 | 17,44–18,76 | 20,21–27,11 |

O carregamento inicial e o tamanho dos buffers recuaram. O P99 em voo
se sobrepõe ao da base, e os máximos continuam variando. O diagnóstico
de picos permanece em CAV-014. CPU e VRAM por quadro ficaram fora da
instrumentação desta comparação. Os CSVs, logs e capturas estão em
`raw/surface/`.

O teste de hidrologia aguardou a retirada real do chunk antes de retornar à
câmara. A água, a edição de um voxel e a pintura persistiram após a
reconstrução. O teste de sessão também passou com a configuração final.
