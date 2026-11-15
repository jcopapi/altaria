# GOAL — SISTEMA DE GERAÇÃO PROCEDURAL DE CAVERNAS

## 1. Objetivo

Desenvolver um sistema completo, modular, extensível e altamente otimizado de geração procedural de cavernas para o jogo em Godot.

A principal inspiração conceitual é o gerador de cavernas do projeto Portolan, especialmente a capacidade de produzir salões subterrâneos gigantescos, redes conectadas, múltiplos níveis de profundidade e formações geológicas naturais.

O sistema deverá incluir:

- Cavernas pequenas, médias e gigantescas.
- Túneis, abismos, fendas e poços verticais.
- Planejamento procedural hierárquico.
- Quatro níveis principais de profundidade.
- Biomas subterrâneos independentes.
- Influência dos biomas da superfície.
- Cavernas subaquáticas.
- Lagos, rios e regiões inundadas.
- Estruturas geológicas variadas.
- Arquitetura extensível para novos biomas e tipos de caverna.
- Preparação para registrar minérios e suas abundâncias futuramente.
- Otimização avançada, carregamento dinâmico e persistência.

**Importante:** as ilhas flutuantes pertencem a uma dimensão separada, não ao mundo principal. O gerador deverá ser integrado inicialmente ao terreno do mundo principal, preservando a possibilidade de reutilização em outras dimensões.

---

# FASE 1 — AUDITORIA DO PROJETO

Antes de implementar qualquer funcionalidade, investigar:

1. Como funciona a geração atual do terreno.
2. Como são representados os voxels, blocos ou volumes.
3. Como funciona o sistema de chunks.
4. Como são definidos os biomas.
5. Como são geradas as meshes e colisões.
6. Como funciona o carregamento e descarregamento de regiões.
7. Como são distribuídas as tarefas entre threads.
8. Como são armazenadas as modificações do jogador.
9. Quais registries e sistemas declarativos já existem.
10. Quais otimizações de geração e renderização já estão implementadas.

O objetivo é integrar as cavernas à arquitetura existente.

Não substituir componentes funcionais sem necessidade técnica comprovada.

Produzir um plano de implementação antes de iniciar modificações estruturais.

---

# FASE 2 — PLANEJAMENTO GLOBAL DAS CAVERNAS

## 2. Gerador hierárquico

As cavernas não deverão ser produzidas exclusivamente por ruído 3D aleatório.

Implementar uma abordagem híbrida:

**Planejamento estrutural**

Determinar antecipadamente:

- Distribuição das redes subterrâneas.
- Localização das câmaras.
- Dimensões aproximadas.
- Profundidades.
- Túneis de conexão.
- Entradas naturais.
- Passagens verticais.
- Possíveis regiões inundadas.
- Condições geológicas.

**Geração geométrica**

Transformar o planejamento em cavernas tridimensionais, utilizando técnicas adequadas, como:

- Signed Distance Fields.
- Ruído 3D.
- Volumes elipsoidais.
- Curvas tridimensionais.
- Operações de união e subtração.
- Deformações procedurais.

O planejamento define a macroestrutura. O ruído acrescenta irregularidades naturais.

Considerar a topografia e a quantidade de rocha disponível. Montanhas altas poderão comportar redes subterrâneas com múltiplos níveis e câmaras monumentais.

A geração deverá ser determinística e independente da ordem de carregamento dos chunks.

Evitar recalcular o planejamento completo repetidamente.

## 3. Tipos de cavernas

Implementar arquétipos configuráveis:

**Cavernas normais**
- Pequenas e médias.
- Formatos irregulares.
- Túneis naturais.
- Frequentes perto da superfície.

**Salões gigantes**
- Grandes extensões horizontais.
- Tetos elevados.
- Formações rochosas monumentais.
- Possibilidade de múltiplos níveis internos.

**Túneis**
- Percursos sinuosos.
- Variações de largura e altura.
- Ramificações.
- Interseções.

**Fendas e ravinas**
- Formações alongadas.
- Grandes desníveis.
- Passagens estreitas ou profundas.

**Poços verticais**
- Conexões entre profundidades.
- Possibilidade de quedas d'água.
- Aplicação de ferramentas de mobilidade.

**Cavernas de desmoronamento**
- Rochas caídas.
- Salões parcialmente bloqueados.
- Formações resultantes de colapsos.

**Cavernas especiais**
- Geodos.
- Formações cristalinas.
- Grandes pilares.
- Formações fósseis e outras anomalias.

Os arquétipos deverão ser extensíveis por registros ou definições de dados.

## 4. Conectividade

Implementar uma estrutura de grafos ou solução equivalente para conectar câmaras subterrâneas.

Garantir:

- Conectividade das principais redes.
- Passagens alternativas quando apropriado.
- Algumas cavernas isoladas intencionalmente.
- Continuidade entre chunks.
- Túneis respeitando as restrições do terreno.
- Entradas e saídas naturais.

Evitar redes inteiras compostas apenas por corredores uniformes.

---

# FASE 3 — NÍVEIS DE PROFUNDIDADE

## 5. Categorias subterrâneas

Implementar quatro categorias principais.

### Nível 1 — Cavernas normais
- Próximas à superfície.
- Maior influência dos biomas externos.
- Túneis e salões variados.
- Formações geológicas comuns.

### Nível 2 — Cavernas profundas
- Câmaras maiores.
- Rios e lagos subterrâneos.
- Ecossistemas próprios.
- Formações geológicas incomuns.

### Nível 3 — Cavernas abissais
- Abismos gigantescos.
- Câmaras monumentais.
- Estruturas e formações especiais.
- Ambientes extremos.

### Nível 4 — Cavernas infernais
- Regiões geotérmicas.
- Rocha vulcânica.
- Lava.
- Formações infernais.
- Condições ambientais extremas.

As transições deverão ser naturais e configuráveis.

Não utilizar fronteiras artificiais abruptas quando for possível criar transições graduais.

A profundidade influencia a geração, mas não será o único fator.

Permitir anomalias raras e regiões especiais.

Não obrigar todas as áreas do mundo a possuírem cavernas infernais.

---

# FASE 4 — BIOMAS SUBTERRÂNEOS

## 6. Influência da superfície

Os biomas superficiais deverão influenciar a aparência e a composição das cavernas.

Exemplos:

**Neve e regiões glaciais**
- Cavernas congeladas.
- Gelo.
- Estalactites congeladas.
- Lagos congelados.
- Superfícies cobertas de neve quando apropriado.

**Desertos**
- Arenito.
- Areia acumulada.
- Formações sedimentares.
- Cavernas secas.
- Aquíferos ocasionais.

**Florestas**
- Musgo.
- Raízes.
- Umidade.
- Vegetação subterrânea.

**Regiões tropicais**
- Cavernas úmidas.
- Raízes grandes.
- Lagos.
- Vegetação abundante.

**Regiões vulcânicas**
- Basalto.
- Obsidiana.
- Fontes termais.
- Formações relacionadas à lava.

A influência da superfície deverá diminuir conforme a profundidade aumenta.

## 7. Biomas subterrâneos independentes

Criar suporte para:

- Florestas de fungos.
- Cavernas cristalinas.
- Cavernas fossilizadas.
- Cavernas inundadas.
- Cavernas abissais.
- Cavernas corrompidas.
- Cavernas hidrotermais.
- Cavernas infernais.

Esses biomas deverão ser determinados por combinações de:

- Profundidade.
- Geologia.
- Temperatura.
- Umidade.
- Presença de água.
- Influência superficial.
- Condições especiais.

Permitir combinações como cavernas cristalinas congeladas ou regiões hidrotermais abissais.

## 8. Registro extensível de biomas

O sistema deverá facilitar a adição de biomas subterrâneos.

Cada bioma poderá declarar:

- Identificador.
- Regras de ocorrência.
- Profundidades permitidas.
- Influência superficial.
- Materiais.
- Características geométricas.
- Decoração.
- Formações especiais.
- Condições ambientais.
- Pontos de extensão para recursos e entidades.

Priorizar definições por dados em vez de grandes estruturas condicionais no código central.

---

# FASE 5 — HIDROLOGIA E CAVERNAS SUBAQUÁTICAS

## 9. Estruturas aquáticas

Implementar suporte a:

- Cavernas totalmente submersas.
- Cavernas parcialmente inundadas.
- Lagos subterrâneos.
- Rios subterrâneos.
- Túneis inundados.
- Grutas conectadas ao oceano.
- Cavernas abissais aquáticas.
- Cavernas cristalinas submersas.
- Regiões hidrotermais.

Uma caverna poderá apresentar regiões secas e inundadas dentro do mesmo sistema.

## 10. Planejamento hidrológico

O sistema deverá:

1. Detectar conexões com massas de água.
2. Identificar depressões subterrâneas.
3. Determinar níveis de água.
4. Planejar regiões inundadas.
5. Criar passagens submersas.
6. Permitir bolsões de ar.
7. Conectar determinados lagos por rios.
8. Impedir vazamentos incorretos.
9. Manter continuidade entre chunks.

Priorizar uma solução determinística e eficiente.

Não implementar uma simulação completa de fluidos caso uma solução mais simples seja suficiente.

Integrar a geração ao sistema de água já existente.

## 11. Preparação para mergulho

As cavernas subaquáticas futuramente incentivarão equipamentos especializados.

Preparar pontos de integração para:

- Oxigênio.
- Pressão.
- Temperatura.
- Visibilidade.
- Resistência ambiental.
- Equipamentos de mergulho.

Não é necessário implementar equipamentos ou mecânicas completas de mergulho neste Goal, salvo dependências mínimas indispensáveis ao funcionamento das cavernas aquáticas.

---

# FASE 6 — PREPARAÇÃO PARA MINÉRIOS

## 12. Registro futuro de minérios e abundância

**Esta fase é exclusivamente arquitetural. Não implementar geração de minérios neste Goal.**

O sistema de cavernas deverá ser preparado para permitir, futuramente, registrar novos minérios e configurar onde eles poderão aparecer.

Deverá ser simples declarar:

- Quais minérios são permitidos em determinado bioma.
- Quais são comuns ou raros.
- Qual é a abundância relativa de cada minério.
- Em quais níveis de profundidade podem aparecer.
- Quais regiões favorecem determinados materiais.
- Quais minérios são exclusivos de determinadas condições.
- Quais regras especiais influenciam sua ocorrência.

Exemplos conceituais:

- Ferro abundante em determinadas cavernas normais.
- Minerais criogênicos exclusivos de cavernas glaciais.
- Materiais vulcânicos favorecidos em regiões infernais.
- Minerais específicos de cavernas subaquáticas.
- Materiais raros associados a determinadas profundidades.

A configuração deverá permitir que diferentes regiões tenham abundâncias distintas, inclusive dentro de um mesmo bioma.

Priorizar uma arquitetura declarativa e compatível com mods.

**Não implementar agora:**

- Posicionamento de minérios no terreno.
- Geração de veios ou jazidas.
- Novos itens de minério.
- Texturas de minérios.
- Mineração de recursos.
- Fundição.
- Criação de ligas.
- Receitas metalúrgicas.
- Sistemas de processamento mineral.

Se já existir um registro de minérios, reutilizá-lo ou preparar sua integração.

Caso contrário, implementar somente a interface ou estrutura de configuração mínima necessária, sem construir um gerador mineral.

Não adicionar complexidade desnecessária por funcionalidades que serão desenvolvidas em outro Goal.

---

# FASE 7 — DESEMPENHO E OTIMIZAÇÃO

## 13. Geração eficiente

Priorizar:

- Planejamento antecipado.
- Geração por chunks.
- Processamento assíncrono seguro.
- Reutilização de resultados.
- Cache de dados.
- Culling.
- LOD quando adequado.
- Meshing otimizado.
- Redução de draw calls.
- Minimização de alocações.
- Evitar cálculos repetidos.

A geração não deverá bloquear a thread principal durante a gameplay.

Os dados das cavernas deverão permanecer consistentes entre chunks.

## 14. Geometria interna

Não renderizar faces completamente ocultas pela rocha.

Entretanto, preservar a representação lógica dos materiais sólidos para permitir futuras modificações, mineração e construção.

Otimizações visuais não poderão comprometer colisões, persistência ou alterações no terreno.

## 15. Carregamento inteligente

Separar:

**Pré-processamento**
- Planejamento subterrâneo.
- Redes de câmaras.
- Conexões.
- Classificação geológica.
- Distribuição regional de biomas.

**Geração local**
- Geometria.
- Meshes.
- Colisões.
- Materiais.
- Decoração.

**Carregamento dinâmico**
- Chunks próximos.
- Pré-carregamento.
- Descarregamento.
- Atualizações locais.

Utilizar medições reais para identificar gargalos.

Um carregamento inicial maior é aceitável quando proporcionar gameplay mais estável.

---

# FASE 8 — PERSISTÊNCIA E INTEGRAÇÃO

## 16. Modificações do jogador

Garantir compatibilidade com:

- Mineração futura.
- Destruição de terreno.
- Construção.
- Bases subterrâneas.
- Máquinas.
- Modificações locais.
- Salvamento.

Não permitir que a regeneração procedural apague modificações persistidas.

## 17. Arquitetura modular

Separar responsabilidades entre:

- Planejamento das redes.
- Geração geométrica.
- Profundidade.
- Biomas.
- Hidrologia.
- Configurações geológicas.
- Geração de chunks.
- Persistência.
- Renderização.

Evitar um único script responsável por todo o sistema.

Respeitar os registries e padrões arquiteturais existentes.

---

# FASE 9 — TESTES E FERRAMENTAS

## 18. Debug

Criar ferramentas para visualizar:

- Redes subterrâneas.
- Câmaras.
- Conexões.
- Profundidades.
- Biomas.
- Áreas inundadas.
- Limites dos chunks.
- Tempos de geração.

## 19. Testes

Validar:

**Determinismo**
- Mesma seed, mesmo resultado.
- Independência da ordem de carregamento.

**Geometria**
- Ausência de cortes entre chunks.
- Conectividade correta.
- Colisões consistentes.
- Ausência de aberturas indesejadas.

**Biomas**
- Distribuição coerente.
- Influência superficial funcional.
- Transições adequadas.

**Hidrologia**
- Lagos consistentes.
- Ausência de vazamentos.
- Conexões aquáticas corretas.

**Persistência**
- Modificações preservadas.
- Salvamento e carregamento funcionais.

**Desempenho**
- Tempo de planejamento.
- Tempo de geração por chunk.
- Consumo de memória.
- Custo de meshes e colisões.
- Picos de frame time.

Não criar testes de geração mineral, pois ela não será implementada agora. Apenas validar a estrutura de configuração, caso seja criada.

---

# FASE 10 — EXECUÇÃO E ENTREGA

## 20. Ordem de implementação

Seguir uma execução incremental:

1. Auditoria técnica.
2. Planejamento global das cavernas.
3. Câmaras e túneis conectados.
4. Geometria e integração com chunks.
5. Profundidade.
6. Biomas subterrâneos.
7. Influência dos biomas superficiais.
8. Hidrologia e cavernas subaquáticas.
9. Pontos de extensão para minérios.
10. Persistência.
11. Otimização.
12. Testes finais.

Não desenvolver todas as funcionalidades simultaneamente.

Cada fase deverá apresentar resultados verificáveis antes de avançar.

## 21. Critérios de conclusão

O Goal estará concluído quando:

- Existirem cavernas procedurais de tamanhos variados.
- Salões gigantes e redes conectadas funcionarem.
- Os quatro níveis de profundidade estiverem integrados.
- Biomas superficiais influenciarem cavernas.
- Biomas subterrâneos independentes funcionarem.
- Cavernas subaquáticas e parcialmente inundadas forem geradas.
- O sistema permitir registrar novos tipos de cavernas e biomas facilmente.
- Existir uma forma clara e extensível de configurar futuramente minérios e abundâncias por região, sem gerar minérios agora.
- Houver integração com chunks e persistência.
- Os testes essenciais passarem.
- O desempenho for medido e otimizado.
- A arquitetura estiver documentada.

## 22. Entrega final

Apresentar:

1. Resumo do que foi implementado.
2. Arquitetura do sistema.
3. Arquivos modificados.
4. Como adicionar novos biomas.
5. Como registrar novos tipos de cavernas.
6. Como futuramente configurar minérios e abundâncias.
7. Resultados dos testes.
8. Medições de desempenho.
9. Limitações conhecidas.

**Diretriz final:** este Goal é dedicado à construção do sistema de cavernas. A geração de minérios, a mineração e a metalurgia serão desenvolvidas separadamente.

Apenas preparar a arquitetura necessária para que esses sistemas sejam integrados futuramente de maneira simples, sem refatorações extensas.