# G3D v1.0 — revisão técnica, correções e esclarecimentos

**Tipo:** documento de revisão, não uma alteração normativa do formato  
**Escopo:** especificação `FORMAT.md`, layout `BINARY.md`, parser, validador, amostras e CI  
**Objetivo:** registrar riscos e decisões pendentes antes de declarar G3D v1.0 estável.

Este documento complementa a especificação. Ele não muda por si só as regras de G3D e não deve ser interpretado como evidência de que os itens listados já foram implementados. As prioridades indicam a ordem sugerida para trabalho.

## 1. Resumo executivo

A direção atual — XML 1.0 convencional para a descrição, pool binário para os dados de geometria e KTX2 para texturas publicadas — é coerente com o objetivo de ter arquivos legíveis durante a autoria e dados eficientes no carregamento.

Os riscos mais importantes são:

1. referências de esqueleto, joint e nó não estão descritas de maneira totalmente consistente;
2. a validação do pool binário ainda precisa comparar metadados com os bytes reais;
3. o parser XML é próprio e limitado, enquanto a especificação promete regras de conformidade mais estritas;
4. algumas regras e exemplos ainda precisam de testes de conformidade específicos;
5. é necessário confirmar a CI no commit atual antes de considerar a revisão validada.

O arquivo `scheme/model.g3d.sch` **não é uma pendência de especificação**. Ele ficou obsoleto quando o projeto abandonou a ideia inicial de uma gramática/estrutura XML modificada e escolheu XML 1.0 convencional. Não se recomenda reativá-lo nem criar uma gramática paralela. Sua remoção do repositório pode ser feita como limpeza de manutenção, caso ainda esteja vazio e sem consumidores, mas não é bloqueio para v1.0.

## 2. Prioridade alta — esclarecer joints, skeletons e nodes

### Problema

A tabela de categorias de IDs em `FORMAT.md` apresenta `joint` e `node` como categorias distintas, mas inclui `skeleton/@nodes` entre as referências de ambas. No validador, os valores de `skeleton/@nodes` são registrados como referências do tipo `node`; na validação do esqueleto, porém, esses mesmos valores são comparados com IDs de `<joint>`.

Isso deixa ambíguo se a lista `nodes` contém IDs de joints, IDs de nós da cena, ou se os dois conceitos deveriam estar ligados por um mapeamento explícito. A amostra pode ocultar o problema quando um joint e um node compartilham o mesmo texto de ID.

### Decisão necessária

Escolher e documentar uma única semântica antes de alterar o código:

- **Opção A — lista de joints:** `skeleton/@nodes` contém IDs de `<joint>`; remover essa referência da categoria `node` e validar somente contra joints do esqueleto.
- **Opção B — lista de nodes da cena:** a lista contém IDs de `<node>), e o formato precisa definir explicitamente como cada node corresponde a um joint e a uma posição nas matrizes de bind.
- **Opção C — mapeamento explícito:** declarar joints e nodes separadamente e armazenar a associação entre eles. É mais expressivo, mas aumenta a complexidade do formato.

Não escolher uma opção só porque a amostra atual passa: incluir testes onde os IDs de joint e node são diferentes, além de testes para referências ausentes e duplicadas.

### IDs de joints

A especificação declara unicidade por categoria em todo o modelo, mas a implementação descrita verifica duplicatas de joints dentro de cada skeleton. Definir se IDs de joints são globais ou locais ao skeleton. A recomendação é manter IDs locais se o formato pretende permitir rigs independentes com nomes comuns; nesse caso, documentar a exceção explicitamente e qualificar referências de joint pelo skeleton quando necessário. Se forem globais, mover a verificação para o escopo global.

**Critério de conclusão:** a regra da especificação, a amostra, o parser e o validador concordam sobre o significado de `skeleton/@nodes`, e os testes cobrem IDs diferentes entre joints e nodes.

## 3. Prioridade alta — validar o pool binário contra os bytes reais

### O que já é possível verificar sem ler o pool

O validador consegue conferir vários metadados declarados em XML, como contagens, tipos de componentes, referências a buffers/views e limites aritméticos. Isso é útil, mas não prova que o arquivo `.bin` existe ou que contém os dados alegados.

### Trabalho necessário

Implementar uma etapa de validação que abra o pool resolvendo `pool/@src` de forma segura e confira:

1. existência do arquivo e tamanho real versus `pool/@byteLength`;
2. limites de cada `buffer`, `bufferView` e stream, incluindo multiplicação e soma com detecção de overflow;
3. `componentType`, número de componentes e stride efetivo para calcular o tamanho de cada stream;
4. índices de triângulos contra `positions.count`, incluindo a interpretação correta do tipo de índice;
5. alinhamento e padding obrigatório zerado;
6. valores decodificados que têm regras semânticas, como pesos de skinning, índices de joints e quantização de posições;
7. bytes excedentes, truncamento e ranges sobrepostos quando proibidos pela especificação.

Todos os cálculos de offset e tamanho devem ser feitos com inteiros sem sinal de largura suficiente e com verificação de overflow **antes** da comparação ou da alocação. Não alocar memória com base em um `count` não validado.

O caminho do pool deve respeitar a política de caminhos de `FORMAT.md`: não aceitar URLs, caminhos absolutos ou escape da raiz do pacote. Para `.g3dp`, a resolução deve ocorrer na tabela de entradas, não no filesystem.

**Critério de conclusão:** fixtures com pool válido, arquivo ausente, arquivo truncado, stream no limite, índice inválido, offset que estoura, padding não zero e referência de buffer inexistente; todos com resultado esperado e diagnóstico útil.

## 4. Prioridade alta — parser XML e contrato de conformidade

### Problema

G3D escolheu XML 1.0 normal, mas o parser próprio não é um processador XML 1.0 completo. A especificação também diz que elementos/atributos desconhecidos do núcleo são erros e proíbe DTDs e entidades externas. Se a implementação não reconhece toda a gramática, aceitar uma entrada não equivale a demonstrar que ela é XML bem-formado ou que está em conformidade com G3D.

### Direção recomendada

Antes de expandir o parser manualmente, avaliar uma biblioteca XML pequena e compatível com os requisitos do projeto. Se a dependência for rejeitada, documentar com precisão o subconjunto aceito e garantir que entradas fora dele falhem com segurança — sem alegar suporte geral a XML 1.0.

Independentemente da estratégia:

- rejeitar DOCTYPE e entidades externas sem resolução de rede ou filesystem;
- rejeitar múltiplas raízes, tags malformadas, atributos duplicados e conteúdo numérico inválido;
- validar elementos e atributos permitidos em cada contexto, não apenas as seções do topo;
- validar nomes de namespace e extensões conforme a regra de extensões, preservando conteúdo desconhecido quando a especificação exigir round-trip;
- impor limites de tamanho, profundidade, contagem de elementos e valores antes de alocar;
- manter os diagnósticos associados à linha e ao elemento responsável.

**Critério de conclusão:** testes positivos e negativos para cada regra estrutural central, incluindo atributos desconhecidos em elementos aninhados, prefixos de extensão inválidos, XML malformado e ataques de expansão/resolução externa.

## 5. Prioridade média — gramática do atributo `when`

O atributo `when` de variantes de textura é uma DSL fechada, não código executável. A implementação deve reconhecer exatamente a gramática documentada, sem interpretar expressões arbitrárias.

Revisar a consistência entre o tokenizador e a validação de `hasExtension()`: se a gramática declara `STRING` e admite strings entre aspas simples ou duplas, ambas precisam ser aceitas de forma consistente; alternativamente, restringir a gramática a aspas duplas e documentar essa escolha.

Adicionar testes para:

- `*`, `formatSupported(NAME)`, `apiLevel >= INT` e `hasExtension(STRING)`;
- `and`, `not` e parênteses;
- strings vazias, escapes, aspas incompatíveis e parênteses não fechados;
- operadores e identificadores desconhecidos;
- expressões malformadas que não devem ser aceitas parcialmente.

A avaliação das condições deve consultar capacidades reais do dispositivo no runtime. A validação offline pode conferir sintaxe e detectar condições estruturalmente impossíveis, mas não deve fingir conhecer os recursos da GPU do usuário.

## 6. Prioridade média — regras que ainda precisam de decisão explícita

### 6.1 Limites numéricos

Consolidar em uma tabela normativa os limites de tamanho, contagem, profundidade, buffers, geometrias, vértices, índices, joints, morph targets e entradas de pacote. Distinguir limites obrigatórios do formato de limites de implementação. Evitar limites divergentes entre parser, validador e leitor de `.g3dp`.

### 6.2 Canais de animação

Finalizar a gramática de `channel`, `sampler`, `input`, `output`, `target` e `interpolation`. Especificar a forma de endereçar morph weights e transformações, a contagem de componentes por propriedade, as regras de tempo e a resolução de IDs. A amostra deve ser validada pelo mesmo conjunto de regras, não apenas servir como documentação visual.

### 6.3 KTX2 e texture sets

A especificação requer inspeção do cabeçalho KTX2, dos níveis e do DFD. Não tratar a mera extensão `.ktx2` como validação suficiente. Criar fixtures reais e pequenas para formatos aceitos, supercompressão proibida, dimensões incompatíveis, variantes divergentes, layers inválidas e arquivo truncado.

### 6.4 Especificação versus implementação

Manter uma matriz de conformidade que liste cada regra normativa, componente responsável (parser, validador semântico, leitor binário, empacotador ou runtime), estado e teste associado. A seção de limitações de `FORMAT.md` deve continuar explícita até que os itens sejam de fato implementados.

## 7. Prioridade média — consistência de UV mapping

A convenção atual do G3D v1.0 é origem de imagem no canto superior esquerdo, com +V para baixo. Essa é uma regra normativa do formato, não uma opção que cada arquivo escolhe.

- O parser deve preservar os valores UV lidos; não deve inverter V automaticamente.
- O exportador deve converter para a convenção G3D se a ferramenta de origem usar uma convenção diferente.
- O renderizador deve aplicar a convenção sem executar uma segunda inversão.
- `uv0` e `uv1` devem ter a mesma contagem de elementos que `positions`.
- UVs em `uint16` normalizado cobrem [0, 1]; UVs que precisam de tiling fora desse intervalo devem usar um tipo de armazenamento que preserve tais valores, como `float32`.
- UVs fora de [0, 1] não devem ser limitados automaticamente pelo validador quando o tipo de armazenamento os permite.
- A orientação dos UVs é independente da convenção de canal verde das normal maps. Não inverter o canal G como efeito colateral da conversão de V.

Criar um teste com textura assimétrica e UVs conhecidos para detectar inversões verticais duplicadas; incluir também uma fixture com UVs fora de [0, 1] e outra com `uv1`.

## 8. Prioridade média — CI, testes e amostras

Antes de declarar uma mudança validada:

1. confirmar que o workflow executou no commit exato a ser integrado;
2. compilar em configuração Debug e Release;
3. executar CTest e garantir que os testes não dependem de `assert()` para reportar falhas;
4. testar os comandos CLI de validação nas amostras positivas e negativas;
5. verificar que as amostras referenciam apenas arquivos existentes, ou que arquivos ausentes estão claramente identificados como fixtures incompletas;
6. manter testes negativos pequenos e independentes, com mensagem esperada ou categoria de erro;
7. adicionar round-trip e determinismo quando o leitor e o empacotador binário existirem.

Uma contagem total de testes, por si só, não demonstra cobertura: registrar quais requisitos cada teste cobre é mais útil para evitar regressões.

## 9. Esclarecimento sobre `scheme/model.g3d.sch`

O arquivo `scheme/model.g3d.sch` pertence à abordagem inicial, na qual se considerava uma estrutura XML modificada ou uma gramática própria para descrever modelos. Essa direção foi abandonada em favor de XML 1.0 convencional com raiz `<g3d version="1.0">`.

Consequentemente:

- não deve ser tratado como schema normativo;
- não é a fonte de verdade da gramática;
- não deve ser preenchido com uma segunda linguagem para competir com XML;
- sua existência vazia não significa que o formato atual esteja incompleto por falta desse arquivo.

A gramática e as regras normativas pertencem a `docs/FORMAT.md` e `docs/BINARY.md`. Se no futuro for útil gerar um schema XML auxiliar para ferramentas de edição, ele deverá ser opcional, derivado da especificação e compatível com XML 1.0 — nunca uma variante de XML. Como o formato inclui regras semânticas entre referências e dados binários, esse schema não substituirá o validador C++.

A ação sugerida é apenas uma limpeza opcional: remover o arquivo obsoleto e quaisquer referências antigas a ele, desde que uma busca no repositório confirme que nenhum processo ainda o consome. Essa limpeza pode ocorrer separadamente e não precisa bloquear o trabalho técnico acima.

## 10. Ordem de execução proposta

| Etapa | Entrega | Critério para avançar |
|---|---|---|
| 1 | Decisão formal sobre skeleton/joint/node | especificação e testes sem ambiguidade |
| 2 | CI verde e matriz de conformidade inicial | resultado confirmado no commit atual |
| 3 | Parser XML mais rigoroso ou biblioteca XML escolhida | testes de gramática e segurança |
| 4 | Validação de pool contra bytes reais | fixtures binárias positivas e negativas |
| 5 | Fechamento de `when`, limites e canais de animação | regras e amostras cobertas por testes |
| 6 | Validação real de KTX2 e texture sets | fixtures KTX2 válidas e inválidas |
| 7 | Testes de UV e documentação de exportação | ausência de inversão dupla e contagens corretas |
| 8 | Leitor/empacotador `.g3dp` e testes de determinismo | round-trip e pacotes byte-idênticos |

Esta ordem prioriza a eliminação de ambiguidades e a confiabilidade do leitor antes de expandir ainda mais o formato. O status geral deve continuar sendo **draft** até que os requisitos normativos essenciais tenham implementação e testes correspondentes.
