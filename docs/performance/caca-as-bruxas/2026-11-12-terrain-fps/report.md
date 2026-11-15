# Terrain, troncos e FPS
## Identificação e reconhecimento
Operação de 12/11/2026 em São Paulo. Ajusta o Terrain, restaura a continuidade
dos troncos e mede a renderização da encosta. Altaria em árvore com alterações
locais, sobre eff109cc. GDstudio 4.7.1, Vulkan Forward+, RTX 4050 Laptop.
## Baseline
Seed 1337, mundo descartável, saída 1280×720, VSync e limite de FPS desligados.
Duas câmeras estáticas, meio-dia, sem vento, após a fila esvaziar e 0,6 s de
aquecimento. 180 frames por câmera, três execuções. Scripts e logs em raw.
Frame time de parede, sem atribuição isolada de CPU/GPU ou VRAM.
## Investigação e plano
Cartógrafo /root/geometry e Sentinela /root/perf, Codex nativo herdado, somente
leitura. Detectaram descarte indevido das faces da forquilha e subdivisões
redundantes da superfície. O coordenador é o único editor de block_shapes,
terrain_mesh e testes. Prioridade para a geometria. Se o cenário das nuvens
continuar caro, medir a rejeição antecipada de amostras como etapa independente.
## Alterações
Mantido o topo plano com uma curva de 40 cm nas bordas expostas. O gerador omite
subdivisões em faixas constantes e triângulos degenerados no upload e na colisão.
A união visual da forquilha preserva as faces expostas. Nenhuma redução da
distância de visão ou da densidade da vegetação.
## Contra-auditoria
Cartógrafo como juiz revisou índices, perfil, emendas e forquilha. A comparação
matemática em 79.007 pontos confirmou a equivalência da tesselação compactada
com a grade completa do perfil novo. Sentinela como caçador executou
TERRAIN_PROFILE_RUNTIME PASS. O mesmo modelo nativo foi herdado pelos revisores.
## Benchmark final
Encosta: medianas iniciais 9,027/9,018/9,071 ms e finais
4,670/4,698/4,708 ms. A contagem de triângulos do jogo caiu de 5.435.308
para 3.213.516 com 334 tiles. O teste headless pode ter coincidido brevemente
com a segunda repetição final, e a melhora também aparece nas duas restantes.
Na altura das nuvens, as medianas caíram de 6,41–6,42 para 3,50–3,52 ms.
O P99 piorou de 7,11–7,22 para 12,38–13,24 ms. A latência de cauda nesse
cenário permanece em aberto. Não atribuir a causa ao shader sem medição isolada.
O teste nativo final passou 224.986 verificações, incluindo ambas as orientações
da forquilha. No voo Vulkan houve 202 quadros amostrados, 193 durante transições
de LOD, e zero amostras de fundo no recorte testado. CAB-003 e CAB-004 registram
a latência residual e o critério para reabrir o problema do tronco.
