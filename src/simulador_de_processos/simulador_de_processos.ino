/*
 * ========================================================
 * PROJETO AVALIADO - SIMULADOR DE PROCESSOS - Arduino UNO
 * ========================================================
 * 
 * @author Andérson & José
 * Descrição: Simula processos e mostra estatisticas dos mesmos
 * Hardware: Arduino UNO + LCD I2C 16x2 + 2 Botões + 2 LEDs + Resistencias (220)
 * 
 * Funcionalidades:
 * - Criar até 10 processos concorrentes
 * - Execução não bloqueante utilizando millis()
 * - Cálculo de estatísticas em tempo real
 * - Execução de processos FIFO
 * - Feedback visual através de LED
 * - Sistema totalmente não bloqueante (sem delay)
 */

// ========================================
// IMPORTAÇÃO DE BIBLIOTECAS
// ========================================
#include <Wire.h>                   // Biblioteca para comunicação I2C
#include <LiquidCrystal_I2C.h>      // Biblioteca para controlo do LCD via I2C

// ========================================
// DEFINIÇÕES DE PINOS
// ========================================
#define BTN_CREATE 2        // Botão 1: Criar novo processo (pino digital 2)
#define BTN_STATS 3         // Botão 2: Mostrar estatísticas (pino digital 3)
#define LED_ACTIVITY 13     // LED indicador de processos ativos (pino digital 13)
#define LED_ALL_DONE 12     // LED indicador de todos os processos concluídos (pino digital 12)

// ========================================
// CONSTANTES GLOBAIS
// ========================================
#define MAX_PROCESSOS 10    // Número máximo de processos que podem ser criados no total
#define TEMPO_MIN 2000      // Tempo mínimo de execução de um processo em milissegundos (2 segundos)
#define TEMPO_MAX 10000     // Tempo máximo de execução de um processo em milissegundos (10 segundos)
#define DEBOUNCE_DELAY 250  // Tempo de debounce do botão em milissegundos para evitar leituras múltiplas

// ========================================
// ESTRUTURA DE PROCESSO
// ========================================
/**
 * @brief Estrutura que representa um processo no sistema
 * 
 * Armazena todas as informações necessárias para gerir
 * o ciclo de vida de um processo simulado
 */
struct Processo {
  int id;                      // Identificador único do processo (1 a 10)
  unsigned long tempoExecucao; // Tempo total de execução previsto em milissegundos
  unsigned long tempoInicio;   // Marca temporal de início da execução (valor de millis())
  bool terminado;              // Estado de conclusão: true = terminado, false = em execução
};

// ========================================
// VARIÁVEIS GLOBAIS
// ========================================

/** @brief Objeto LCD configurado para endereço I2C 0x27, 16 colunas e 2 linhas */
LiquidCrystal_I2C lcd(0x27, 16, 2);

/** @brief Array que armazena todos os processos (máximo 10 posições) em ordem FIFO */
Processo processos[MAX_PROCESSOS];

/** @brief Contador sequencial para atribuir IDs únicos aos processos (começa em 1) */
int proximoID = 1;

// Gestão de estado dos botões para debouncing
/** @brief Marca temporal da última pressão do botão CREATE */
unsigned long lastBtn1Press = 0;
/** @brief Marca temporal da última pressão do botão STATS */
unsigned long lastBtn2Press = 0;

// Modo de visualização do LCD
/** @brief Enumeração dos modos de visualização do display */
enum DisplayMode { MODE_NORMAL, MODE_STATS };
/** @brief Modo de visualização atual do LCD */
DisplayMode currentMode = MODE_NORMAL;
/** @brief Marca temporal de quando as estatísticas começaram a ser exibidas */
unsigned long statsDisplayTime = 0;
/** @brief Duração da exibição das estatísticas no LCD (5 segundos) */
#define STATS_DISPLAY_DURATION 5000

// ========================================
// MÁQUINA DE ESTADOS PARA MENSAGENS NÃO BLOQUEANTES
// ========================================
/**
 * @brief Enumeração dos estados da máquina de estados para mensagens temporárias
 * 
 * Permite exibir mensagens no LCD sem bloquear o loop() principal,
 * substituindo o uso de delay() por controlo temporal com millis()
 */
enum MessageState {
  MSG_IDLE,              // Nenhuma mensagem ativa - sistema operacional normal
  MSG_WELCOME,           // Mensagem de boas-vindas durante o arranque
  MSG_READY,             // Sistema pronto para operar
  MSG_PROCESS_CREATED,   // Feedback de processo criado com sucesso
  MSG_LIMIT_REACHED,     // Aviso de limite máximo de processos atingido
  MSG_NO_PROCESSES,      // Aviso de que não existem processos para mostrar estatísticas
  MSG_STATS_SCREEN1,     // Estatísticas ecrã 1: Total e Média
  MSG_STATS_SCREEN2,     // Estatísticas ecrã 2: Processo com menor tempo
  MSG_STATS_SCREEN3      // Estatísticas ecrã 3: Processo com maior tempo
};

/** @brief Estado atual da máquina de estados de mensagens */
MessageState currentMessageState = MSG_IDLE;
/** @brief Marca temporal de início da mensagem atual */
unsigned long messageStartTime = 0;
/** @brief Duração configurada para a mensagem atual em milissegundos */
unsigned long messageDuration = 0;

// Variáveis temporárias para dados de estatísticas (partilhadas entre estados)
/** @brief ID do processo com menor tempo de execução */
int tempIdMenorTempo = -1;
/** @brief ID do processo com maior tempo de execução */
int tempIdMaiorTempo = -1;
/** @brief Menor tempo de execução encontrado em milissegundos */
unsigned long tempMenorTempo = 0;
/** @brief Maior tempo de execução encontrado em milissegundos */
unsigned long tempMaiorTempo = 0;
/** @brief Tempo médio de execução calculado em milissegundos */
unsigned long tempMedia = 0;
/** @brief Contador de processos presentes no array (usado para média) */
int tempContador = 0;
/** @brief ID do processo recém-criado (para exibição temporária) */
int tempProcessoID = 0;
/** @brief Tempo de execução do processo recém-criado em milissegundos */
unsigned long tempProcessoTempo = 0;

/** @brief Flag que indica se a sequência de inicialização (setup) está completa */
bool setupCompleto = false;

// ========================================
// FUNÇÃO SETUP
// ========================================
/**
 * @brief Função de inicialização do Arduino (executada uma única vez)
 * 
 * Configura todos os periféricos necessários:
 * - Comunicação série para depuração
 * - Pinos de entrada (botões) e saída (LEDs)
 * - Display LCD I2C
 * - Array de processos
 * - Inicia a sequência de mensagens de arranque não bloqueante
 */
void setup() {
  // Inicializar comunicação série a 9600 baud para depuração via Serial Monitor
  Serial.begin(9600);
  
  // Configurar pinos digitais
  pinMode(BTN_CREATE, INPUT_PULLUP);   // Botão com resistência pull-up interna (LOW quando pressionado)
  pinMode(BTN_STATS, INPUT_PULLUP);    // Botão com resistência pull-up interna (LOW quando pressionado)
  pinMode(LED_ACTIVITY, OUTPUT);       // LED de atividade configurado como saída
  pinMode(LED_ALL_DONE, OUTPUT);       // LED de conclusão configurado como saída
  
  // Inicializar LCD I2C e ativar retroiluminação
  lcd.init();
  lcd.backlight();
  
  // Inicializar semente do gerador de números aleatórios
  // Usa ruído analógico do pino A0 para gerar valores pseudo-aleatórios diferentes a cada arranque
  randomSeed(analogRead(A0));
  
  // Inicializar todos os processos do array para o estado vazio
  inicializarProcessos();
  
  // Iniciar sequência de mensagens de inicialização não bloqueante
  currentMessageState = MSG_WELCOME;   // Primeiro estado: mensagem de boas-vindas
  messageStartTime = millis();         // Registar tempo de início
  messageDuration = 2000;              // Duração: 2 segundos
  
  // Exibir mensagem de boas-vindas no LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Process Manager");
  lcd.setCursor(0, 1);
  lcd.print("Iniciando...");
  
  // Mensagem de arranque no Serial Monitor
  Serial.println("=== Gestor de Processos Iniciado ===");
}

// ========================================
// CICLO PRINCIPAL
// ========================================
/**
 * @brief Ciclo principal do programa (executado continuamente)
 * 
 * Gere todas as tarefas do sistema de forma não bloqueante:
 * - Máquina de estados de mensagens
 * - Leitura de botões com debouncing
 * - Execução de processos ativos
 * - Atualização de LEDs
 * - Atualização do display LCD
 * 
 * O loop nunca é bloqueado por delay(), garantindo resposta em tempo real
 */
void loop() {
  // Obter marca temporal atual para todas as operações temporizadas
  unsigned long currentMillis = millis();
  
  // Processar máquina de estados de mensagens (sempre ativa)
  processarMensagens(currentMillis);
  
  // Só processar o sistema principal após conclusão do setup
  if (setupCompleto) {
    // Processar entradas dos botões com debouncing
    handleButtons(currentMillis);
    
    // Executar processos ativos de forma não bloqueante
    executarProcessos(currentMillis);
    
    // Atualizar estado do LED de atividade
    atualizarLED();
    
    // Atualizar estado do LED de conclusão
    atualizarLEDConclusao();
    
    // Atualizar display LCD apenas quando não há mensagem temporária ativa
    if (currentMessageState == MSG_IDLE) {
      atualizarDisplay(currentMillis);
    }
  }
}

// ========================================
// FUNÇÕES DE INICIALIZAÇÃO
// ========================================

/**
 * @brief Inicializa todos os processos do array para o estado vazio
 * 
 * Define todos os campos como zero/vazio e marca como terminado,
 * preparando o sistema para começar a criar novos processos
 */
void inicializarProcessos() {
  for (int i = 0; i < MAX_PROCESSOS; i++) {
    processos[i].id = 0;               // ID zero indica slot vazio
    processos[i].tempoExecucao = 0;    // Tempo de execução zerado
    processos[i].tempoInicio = 0;      // Marca temporal zerada
    processos[i].terminado = true;     // Marcado como terminado (disponível)
  }
}

// ========================================
// MÁQUINA DE ESTADOS PARA MENSAGENS
// ========================================

/**
 * @brief Processa a máquina de estados de mensagens não bloqueantes
 * 
 * Substitui todos os delay() por temporização com millis(), permitindo
 * que o sistema continue responsivo durante a exibição de mensagens.
 * Gere transições entre estados quando o tempo de cada mensagem expira.
 * 
 * @param currentMillis Marca temporal atual em milissegundos
 */
void processarMensagens(unsigned long currentMillis) {
  // Verificar se há uma mensagem ativa e se o seu tempo expirou
  if (currentMessageState != MSG_IDLE) {
    if (currentMillis - messageStartTime >= messageDuration) {
      // Avançar para o próximo estado conforme o estado atual
      switch (currentMessageState) {
        case MSG_WELCOME:
          // Após mensagem de boas-vindas, transitar para "Sistema Pronto"
          currentMessageState = MSG_READY;
          messageStartTime = currentMillis;
          messageDuration = 1000;  // 1 segundo
          
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Sistema Pronto");
          lcd.setCursor(0, 1);
          lcd.print("Processos: 0");
          break;
          
        case MSG_READY:
          // Setup completo - sistema pronto para operar normalmente
          currentMessageState = MSG_IDLE;
          setupCompleto = true;
          break;
          
        case MSG_PROCESS_CREATED:
          // Feedback de criação exibido - voltar ao modo normal
          currentMessageState = MSG_IDLE;
          break;
          
        case MSG_LIMIT_REACHED:
          // Aviso de limite exibido - voltar ao modo normal
          currentMessageState = MSG_IDLE;
          break;
          
        case MSG_NO_PROCESSES:
          // Aviso de ausência de processos exibido - voltar ao modo normal
          currentMessageState = MSG_IDLE;
          break;
          
        case MSG_STATS_SCREEN1:
          // Ecrã 1 de estatísticas concluído - avançar para ecrã 2 (Menor tempo)
          currentMessageState = MSG_STATS_SCREEN2;
          messageStartTime = currentMillis;
          messageDuration = 2500;  // 2.5 segundos
          
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Menor Tempo:");
          lcd.setCursor(0, 1);
          lcd.print("ID:");
          lcd.print(tempIdMenorTempo);
          lcd.print(" T:");
          lcd.print(tempMenorTempo / 1000);
          lcd.print(".");
          lcd.print((tempMenorTempo % 1000) / 100);
          lcd.print("s");
          break;
          
        case MSG_STATS_SCREEN2:
          // Ecrã 2 de estatísticas concluído - avançar para ecrã 3 (Maior tempo)
          currentMessageState = MSG_STATS_SCREEN3;
          messageStartTime = currentMillis;
          messageDuration = 2500;  // 2.5 segundos
          
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Maior Tempo:");
          lcd.setCursor(0, 1);
          lcd.print("ID:");
          lcd.print(tempIdMaiorTempo);
          lcd.print(" T:");
          lcd.print(tempMaiorTempo / 1000);
          lcd.print(".");
          lcd.print((tempMaiorTempo % 1000) / 100);
          lcd.print("s");
          break;
          
        case MSG_STATS_SCREEN3:
          // Sequência de estatísticas concluída - voltar ao modo normal
          currentMessageState = MSG_IDLE;
          currentMode = MODE_STATS;
          statsDisplayTime = currentMillis;
          break;
          
        default:
          // Estado desconhecido - voltar ao modo idle por segurança
          currentMessageState = MSG_IDLE;
          break;
      }
    }
  }
}

// ========================================
// GESTÃO DE BOTÕES
// ========================================

/**
 * @brief Processa pressões de botões com debouncing
 * 
 * Lê o estado dos botões e executa ações correspondentes apenas
 * quando o tempo de debounce foi respeitado. Ignora pressões durante
 * exibição de mensagens para evitar conflitos no LCD.
 * 
 * @param currentMillis Marca temporal atual em milissegundos
 */
void handleButtons(unsigned long currentMillis) {
  // Só processar botões se não houver mensagem temporária ativa
  // Previne conflitos de exibição no LCD
  if (currentMessageState != MSG_IDLE) {
    return;
  }
  
  // Botão 1: Criar Processo
  // INPUT_PULLUP significa que LOW = pressionado
  if (digitalRead(BTN_CREATE) == LOW) {
    // Verificar se passou tempo suficiente desde a última pressão (debouncing)
    if (currentMillis - lastBtn1Press > DEBOUNCE_DELAY) {
      lastBtn1Press = currentMillis;  // Atualizar marca temporal
      criarProcesso(currentMillis);   // Executar ação
    }
  }
  
  // Botão 2: Mostrar Estatísticas
  if (digitalRead(BTN_STATS) == LOW) {
    // Verificar debouncing
    if (currentMillis - lastBtn2Press > DEBOUNCE_DELAY) {
      lastBtn2Press = currentMillis;      // Atualizar marca temporal
      mostrarEstatisticas(currentMillis); // Executar ação
    }
  }
}

// ========================================
// FUNÇÕES DE GESTÃO DE PROCESSOS
// ========================================

/**
 * @brief Cria um novo processo no sistema
 * 
 * Verifica primeiro se o limite histórico de 10 processos foi atingido.
 * Se houver capacidade, procura um slot livre (FIFO), atribui um ID único,
 * gera um tempo de execução aleatório e inicia o processo.
 * 
 * @param currentMillis Marca temporal atual em milissegundos (usado como tempo de início)
 * @return true se o processo foi criado com sucesso, false se o limite foi atingido
 */
bool criarProcesso(unsigned long currentMillis) {
  
  // ========================================
  // VERIFICAR LIMITE HISTÓRICO PRIMEIRO
  // ========================================
  // Se já foram criados 10 processos no total (proximoID - 1 >= 10),
  // bloquear IMEDIATAMENTE, independentemente de haver slots livres.
  // Isto garante que o sistema respeita o limite máximo absoluto.
  if (proximoID - 1 >= MAX_PROCESSOS) {
    Serial.println("ERRO: Número máximo de processos atingido!");
    
    // Mostrar mensagem de limite atingido (não bloqueante)
    currentMessageState = MSG_LIMIT_REACHED;
    messageStartTime = currentMillis;
    messageDuration = 1500;  // 1.5 segundos
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("LIMITE ATINGIDO");
    lcd.setCursor(0, 1);
    lcd.print("Max: 10 processos");
    
    return false;  // Sair da função SEM criar processo
  }
  // ========================================
  
  // Procurar primeira posição disponível no array (FIFO)
  // Um slot está livre se id == 0 ou se o processo terminou
  int slot = -1;
  for (int i = 0; i < MAX_PROCESSOS; i++) {
    if (processos[i].id == 0 || processos[i].terminado) {
      slot = i;  // Slot livre encontrado
      break;     // Parar pesquisa (FIFO: primeira posição livre)
    }
  }
  
  // Criar novo processo no slot encontrado
  // (A verificação de slot == -1 não é necessária porque se proximoID <= 10,
  //  sempre haverá pelo menos um slot disponível)
  processos[slot].id = proximoID;                              // Atribuir ID sequencial único
  processos[slot].tempoExecucao = random(TEMPO_MIN, TEMPO_MAX + 1);  // Tempo aleatório entre 2s e 10s
  processos[slot].tempoInicio = currentMillis;                 // Marcar tempo de início
  processos[slot].terminado = false;                           // Marcar como em execução
  
  // Registar criação no Serial Monitor para depuração
  Serial.print("Processo CRIADO: ID=");
  Serial.print(proximoID);
  Serial.print(" | Tempo=");
  Serial.print(processos[slot].tempoExecucao);
  Serial.println("ms");
  
  // Guardar dados temporários para exibir na mensagem de feedback
  tempProcessoID = processos[slot].id;
  tempProcessoTempo = processos[slot].tempoExecucao;
  
  // Incrementar contador de ID para o próximo processo
  proximoID++;
  
  // Mostrar feedback visual no LCD (não bloqueante)
  currentMessageState = MSG_PROCESS_CREATED;
  messageStartTime = currentMillis;
  messageDuration = 1500;  // 1.5 segundos
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Processo Criado");
  lcd.setCursor(0, 1);
  lcd.print("ID: ");
  lcd.print(tempProcessoID);
  lcd.print(" T:");
  lcd.print(tempProcessoTempo / 1000);  // Converter para segundos
  lcd.print("s");
  
  return true;  // Processo criado com sucesso
}

/**
 * @brief Executa todos os processos ativos de forma não bloqueante
 * 
 * Percorre o array de processos e verifica se cada processo ativo
 * já atingiu o seu tempo de execução previsto. Se sim, marca-o como terminado.
 * Usa millis() para cálculo de tempo decorrido sem bloquear o sistema.
 * 
 * @param currentMillis Marca temporal atual em milissegundos
 */
void executarProcessos(unsigned long currentMillis) {
  // Percorrer todos os slots do array
  for (int i = 0; i < MAX_PROCESSOS; i++) {
    // Ignorar processos vazios (id == 0) ou já terminados
    if (processos[i].id == 0 || processos[i].terminado) {
      continue;
    }
    
    // Calcular tempo decorrido desde o início do processo
    unsigned long tempoDecorrido = currentMillis - processos[i].tempoInicio;
    
    // Verificar se o processo já cumpriu o seu tempo de execução
    if (tempoDecorrido >= processos[i].tempoExecucao) {
      processos[i].terminado = true;  // Marcar como terminado
      
      // Registar conclusão no Serial Monitor
      Serial.print("Processo TERMINADO: ID=");
      Serial.println(processos[i].id);
    }
  }
}

/**
 * @brief Conta quantos processos estão atualmente em execução (ativos)
 * 
 * Um processo é considerado ativo se tiver ID válido (diferente de zero)
 * e não estiver marcado como terminado.
 * 
 * @return Número de processos ativos no momento
 */
int contarProcessosAtivos() {
  int ativos = 0;
  for (int i = 0; i < MAX_PROCESSOS; i++) {
    // Processo ativo: tem ID e não está terminado
    if (processos[i].id != 0 && !processos[i].terminado) {
      ativos++;
    }
  }
  return ativos;
}

/**
 * @brief Retorna o total histórico de processos criados desde o início
 * 
 * Como cada processo recebe um ID sequencial único (proximoID++),
 * o total de processos criados é simplesmente (proximoID - 1).
 * Este valor nunca diminui, mesmo quando processos terminam ou
 * slots são reutilizados.
 * 
 * @return Número total de processos criados desde o arranque do sistema
 */
int contarProcessosCriados() {
  // proximoID começa em 1, então proximoID - 1 = total criado
  // Exemplo: se proximoID = 5, foram criados 4 processos (IDs 1, 2, 3, 4)
  return proximoID - 1;
}

/**
 * @brief Verifica se todos os processos criados estão concluídos
 * 
 * Retorna true apenas se:
 * 1. Pelo menos um processo foi criado
 * 2. Todos os processos com ID válido estão marcados como terminados
 * 
 * @return true se todos os processos estão concluídos, false caso contrário
 */
bool todosProcessosConcluidos() {
  int totalCriados = contarProcessosCriados();
  
  // Se não existem processos criados, retornar false
  if (totalCriados == 0) {
    return false;
  }
  
  // Verificar se todos os processos com ID válido estão terminados
  for (int i = 0; i < MAX_PROCESSOS; i++) {
    // Se encontrar um processo válido que não está terminado
    if (processos[i].id != 0 && !processos[i].terminado) {
      return false;  // Ainda há processos em execução
    }
  }
  
  return true;  // Todos os processos estão concluídos
}

// ========================================
// FUNÇÕES DE ESTATÍSTICAS
// ========================================

/**
 * @brief Calcula e exibe estatísticas abrangentes dos processos
 * 
 * Implementação não bloqueante usando máquina de estados para exibir
 * múltiplos ecrãs de estatísticas sequencialmente:
 * - Ecrã 1: Total de processos e tempo médio
 * - Ecrã 2: Processo com menor tempo
 * - Ecrã 3: Processo com maior tempo
 * 
 * As estatísticas são calculadas apenas com os processos presentes
 * no array (com ID válido), mas o total histórico é obtido de proximoID.
 * 
 * @param currentMillis Marca temporal atual em milissegundos
 */
void mostrarEstatisticas(unsigned long currentMillis) {

  // Obter total histórico de processos criados
  int totalHistorico = proximoID - 1;

  // Verificar se algum processo foi alguma vez criado
  if (totalHistorico == 0) {
    // Nenhum processo criado - mostrar mensagem apropriada
    currentMessageState = MSG_NO_PROCESSES;
    messageStartTime = currentMillis;
    messageDuration = 1500;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SEM PROCESSOS");
    lcd.setCursor(0, 1);
    lcd.print("Crie um primeiro");
    return;  // Sair da função
  }
  
  // Calcular estatísticas apenas com os processos presentes no array
  // (processos reutilizados/limpos já não têm dados disponíveis)
  tempIdMenorTempo = -1;              // Inicializar ID do menor
  tempIdMaiorTempo = -1;              // Inicializar ID do maior
  tempMenorTempo = TEMPO_MAX + 1;     // Inicializar com valor maior que o máximo possível
  tempMaiorTempo = 0;                 // Inicializar com zero
  unsigned long somaTempos = 0;       // Acumulador para cálculo da média
  tempContador = 0;                   // Contador de processos válidos no array
  
  // Percorrer array procurando processos válidos e calculando estatísticas
  for (int i = 0; i < MAX_PROCESSOS; i++) {
    // Processar apenas slots com ID válido (processo criado)
    if (processos[i].id != 0) {
      unsigned long tempo = processos[i].tempoExecucao;
      
      // Verificar se é o menor tempo encontrado até agora
      if (tempo < tempMenorTempo) {
        tempMenorTempo = tempo;
        tempIdMenorTempo = processos[i].id;
      }
      
      // Verificar se é o maior tempo encontrado até agora
      if (tempo > tempMaiorTempo) {
        tempMaiorTempo = tempo;
        tempIdMaiorTempo = processos[i].id;
      }
      
      // Acumular tempo para cálculo da média
      somaTempos += tempo;
      tempContador++;  // Contar processos válidos
    }
  }
  
  // Calcular média com proteção contra divisão por zero
  tempMedia = (tempContador > 0) ? (somaTempos / tempContador) : 0;
  
  // Enviar estatísticas completas para o Serial Monitor
  Serial.println("=== Estatisticas ===");
  Serial.print("Menor: ID=");
  Serial.print(tempIdMenorTempo);
  Serial.print(" Tempo=");
  Serial.println(tempMenorTempo);
  Serial.print("Maior: ID=");
  Serial.print(tempIdMaiorTempo);
  Serial.print(" Tempo=");
  Serial.println(tempMaiorTempo);
  Serial.print("Media: ");
  Serial.println(tempMedia);
  Serial.println("====================");
  
  // Iniciar sequência de ecrãs de estatísticas no LCD (não bloqueante)
  // Ecrã 1: Total histórico e Média
  currentMessageState = MSG_STATS_SCREEN1;
  messageStartTime = currentMillis;
  messageDuration = 2500;  // 2.5 segundos
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Total: ");
  lcd.print(totalHistorico);  // Mostra o total histórico (proximoID - 1)
  lcd.setCursor(0, 1);
  lcd.print("Media: ");
  // Formatar média como X.Xs (exemplo: 5.3s)
  lcd.print(tempMedia / 1000);
  lcd.print(".");
  lcd.print((tempMedia % 1000) / 100);
  lcd.print("s");
}

// ========================================
// FUNÇÕES DE VISUALIZAÇÃO
// ========================================

/**
 * @brief Atualiza o display LCD com base no modo atual
 * 
 * Controla a taxa de atualização do LCD (500ms) para evitar cintilação.
 * Gere também a transição automática do modo STATS para MODE_NORMAL
 * após o tempo definido em STATS_DISPLAY_DURATION.
 * 
 * @param currentMillis Marca temporal atual em milissegundos
 */
void atualizarDisplay(unsigned long currentMillis) {
  static unsigned long lastUpdate = 0;
  
  // Limitar atualização a cada 500ms para evitar cintilação do LCD
  if (currentMillis - lastUpdate < 500) {
    return;
  }
  lastUpdate = currentMillis;
  
  // Verificar se é hora de retornar ao modo normal após exibir estatísticas
  if (currentMode == MODE_STATS && 
      currentMillis - statsDisplayTime > STATS_DISPLAY_DURATION) {
    currentMode = MODE_NORMAL;
  }
  
  // Exibir conteúdo apropriado conforme o modo
  if (currentMode == MODE_NORMAL) {
    mostrarDisplayNormal();
  }
}

/**
 * @brief Exibe o display de operação normal no LCD
 * 
 * Mostra informação em tempo real:
 * - Linha 1: Total de processos criados / Máximo permitido
 * - Linha 2: Número de processos ativos + indicador visual (*)
 */
void mostrarDisplayNormal() {
  // Obter contagens atuais
  int ativos = contarProcessosAtivos();
  int criados = contarProcessosCriados();
  
  lcd.clear();
  
  // Linha 1: Total de processos (histórico / máximo)
  lcd.setCursor(0, 0);
  lcd.print("Total: ");
  lcd.print(criados);
  lcd.print("/");
  lcd.print(MAX_PROCESSOS);
  
  // Linha 2: Processos ativos
  lcd.setCursor(0, 1);
  lcd.print("Ativos: ");
  lcd.print(ativos);
  
  // Mostrar indicador visual (*) se houver processos ativos
  if (ativos > 0) {
    lcd.setCursor(15, 1);  // Canto direito da linha 2
    lcd.print("*");
  }
}

// ========================================
// CONTROLO DO LED
// ========================================

/**
 * @brief Atualiza o estado do LED de atividade
 * 
 * Regras:
 * - LED LIGADO (HIGH): Existe pelo menos um processo em execução
 * - LED DESLIGADO (LOW): Todos os processos estão terminados ou não há processos
 */
void atualizarLED() {
  int ativos = contarProcessosAtivos();
  
  if (ativos > 0) {
    digitalWrite(LED_ACTIVITY, HIGH);  // Acender LED
  } else {
    digitalWrite(LED_ACTIVITY, LOW);   // Apagar LED
  }
}

/**
 * @brief Atualiza o estado do LED de conclusão
 * 
 * Regras:
 * - LED LIGADO (HIGH): Existem processos E todos estão concluídos
 * - LED DESLIGADO (LOW): Não existem processos OU há processos ainda ativos
 * 
 * Este LED só acende quando todos os trabalhos estão completos
 */
void atualizarLEDConclusao() {
  if (todosProcessosConcluidos()) {
    digitalWrite(LED_ALL_DONE, HIGH);  // Acender LED
  } else {
    digitalWrite(LED_ALL_DONE, LOW);   // Apagar LED
  }
}

// ========================================
// FIM DO PROGRAMA
// ========================================