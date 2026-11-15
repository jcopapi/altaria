# Exploração de cavernas e desempenho

## Base e método

Operação de 11/11/2026. Base Git local eff109c com a superfície v3 e a
reforma de cavernas já compiladas. GDstudio 4.7.1, Vulkan 1.4.329,
Forward+, RTX 4050 Laptop, 1280×720. Os testes usaram projetos e diretórios
XDG descartáveis. O coordenador escreveu as alterações. Os agentes
`/root/cave_architecture_audit` e `/root/cave_runtime_audit` revisaram em
leitura, com modelo herdado.

O benchmark carregou a seed 1337, aguardou fila vazia, manteve a câmera
parada por 2 s e voou por 4 s a (85,0,20) m/s a partir de (100,200,100).
VSync desligado e limite de 120 FPS. A biblioteca antiga e a final usaram
os mesmos JSONs para isolar o desempenho. A geração nova foi validada
separadamente. O script e as amostras estão em `raw/`.

## Cavernas e planícies

- As profundidades alvo passaram a 44, 132, 300 e 525 m. A largura das
  regiões caiu de 256 para 224 m, com probabilidade maior de acesso. As
  ligações canônicas e a divisão de bacias permanecem no gerador.
- Ramais opcionais com raio inferior a 2,5 m terminam em alcovas. O
  candidato rejeita sobreposição do fundo com a geometria conhecida do
  plano. O teste percorreu 147 ramais em 75 regiões de três seeds.
- As bocas procuram um desnível com teto natural e usam o caminho
  horizontal para encontrar o exterior. As ravinas receberam curvas
  e os poços ganharam entradas inclinadas. Em três seeds, o teste
  verificou 56 entradas e sua ligação a salões gigantes, com 7 bocas
  laterais, 14 ravinas e 35 poços. Para as bocas, o teste confirmou
  rocha original na parte interna, passagem escavada e uma saída
  quase horizontal.
- A amostra nativa chegou a Y = −542, frente a Y = −708 no perfil
  anterior. No Godot, a câmara validada da seed 1337 estava 551,5 m
  abaixo do relevo. A entrada lateral renderizada fica perto de
  (−67, 203, 524).
- A inspiração de r/gamedesign foi aplicada a caminhos de larguras
  distintas e grupos de musgo, raízes, fungos e estalactites de calcário.
  O catálogo ativo seleciona essas formas separadamente da variação de
  rochas, com apoio conferido nos voxels do host. Na cena renderizada,
  um fungo tinha 99 voxels apoiados e foram verificados os blocos de
  copa e musgo. A captura interna usa luz de inspeção temporária.
  A lista de materiais de cor sólida está em `docs/caves/ASSETS.md`.
- O teste de relevo verificou morros locais de mais de 3 m em áreas
  construíveis, limitados pela amplitude configurada de 9 m.

## Handoff aplicado

| Item | Alteração e evidência |
| --- | --- |
| BIOM-01 | A coluna final define as temperaturas de dia e noite. O teste verificou temperatura ambiente abaixo de zero ao meio-dia no topo nevado, sem influência de blocos. A taxa de lapso existente continua configurável. |
| PROC-01 | Um lote FastNoise2 amostra os candidatos das árvores, com reutilização da coluna durante a busca do solo. Três seeds e 100 chunks por seed produziram contagens e hashes idênticos. O tempo por 100 chunks caiu de 17,67–41,30 ms para 0,88–3,77 ms nas repetições. |
| PROC-02 | A corrente fluvial consulta a coluna uma vez. O teste preservou correnteza e simulação de líquidos. |
| PERF-01 | A face visual abaixo do piso foi suprimida. O greedy já reduzia a bedrock uniforme a um quad por tile, o que limita a diferença de triângulos. A face física continua no collider. |
| MEM-01 | Chunks cobertos e distantes saem do mapa de streaming após um salto longo. A destruição de nós segue em lotes. Colliders em retirada perdem camada e máscara. Jobs em andamento ainda podem reter seus próprios volumes. |
| FIS-01 e FIS-02 | Jolt cria terreno quando um corpo próximo precisa, usando a caixa do corpo transformada e checando a revisão do chunk. O movimento do jogador continua com voxels. No teste sem corpos, o contador de colliders ficou em zero. Um bloco caiu até o apoio com um collider ativo entre 44 chunks LOD0. |
| PERF-02 | A conversão para PackedArrays reutiliza buffers separados para sólidos, líquidos, overlays e peças. Upload de malha e criação sob demanda compartilham o orçamento de quadro existente. |
| PERF-03 | A seleção atualiza imediatamente quando a câmera muda ou acompanha um corpo físico. Parada, consulta a 30 Hz, e a ação do clique recalcula o alvo. |

O mesher também passou a percorrer os registros esparsos de formas e a usar
uma máscara de bits para consultá-los no greedy. Os parâmetros de iluminação
e fog acompanham a atualização climática. O relógio do vento continua por
quadro. A correção térmica combina as influências físicas e construídas uma vez.

As propostas de fase 3 foram revisadas no código atual. A densidade já usa
lotes vetoriais seletivos na faixa da superfície. Os LODs visuais descartam
o volume, e os volumes lógicos compactam rocha uniforme. O cache de cavernas
já tem LRU de 64 regiões com limite adicional em bytes. Um experimento de
coalescer misses concorrentes colocou os workers em espera na carga fria e
foi retirado.

## Quadros e regressões

| Biblioteca | Tempo até 444 tiles prontos | P99 em voo | Máximo em voo |
| --- | ---: | ---: | ---: |
| Base, três rodadas | 3863–4772 ms | 16,97–38,12 ms | 19,28–45,01 ms |
| Final, duas rodadas | 5199–5830 ms | 9,31–9,38 ms | 12,07–12,79 ms |

O custo de construção Jolt durante o voo caiu a zero. A rodada instrumentada
da base registrou máximo de 34,96 ms só em Jolt. A fila final ainda tinha
19–24 trabalhos, e a carga inicial aumentou nesta amostra. O resultado do
P99 demonstra a melhora no ritmo dos quadros no percurso. O tempo de
conclusão do streaming e o custo máximo de upload continuam abertos em CAV-014.
O RSS do processo foi registrado. CPU e VRAM por quadro ficaram fora da
instrumentação.

A compilação da extensão e 231.423 verificações nativas passaram. Os testes
de console, sessão, rotas das cavernas e hidrologia Jolt passaram. O teste
renderizado confirmou abertura lateral, câmara funda, formação com os
materiais novos e queda do bloco. A revisão independente também registrou
dois limites. Corpos fora da cobertura lógica aguardam chunks próximos. Uma
edição de aparência ainda pode invalidar a revisão do collider do tile.

O teste de teletransporte confirmou quatro colliders demandados ao redor
de um corpo que caiu na interseção de chunks. Após um salto para (16000,
210, 16000), os buffers residentes caíram de 18.761.880 para zero bytes
em três quadros, e os nós antigos em retirada estavam sem camada ou
máscara de colisão. Os chunks do destino carregam em seguida.
