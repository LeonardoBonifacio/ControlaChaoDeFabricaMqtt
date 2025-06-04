#include "pico/stdlib.h"            // Biblioteca da Raspberry Pi Pico para funções padrão (GPIO, temporização, etc.)
#include "pico/cyw43_arch.h"        // Biblioteca para arquitetura Wi-Fi da Pico com CYW43
#include "pico/unique_id.h"         // Biblioteca com recursos para trabalhar com os pinos GPIO do Raspberry Pi Pico
#include "pico/bootrom.h"           // Modo bootsel pelo botão para desenvolver mais rapidamente
#include "pico/stdlib.h"            // Definições padroes para pico

#include "lwip/apps/mqtt.h"         // Biblioteca LWIP MQTT -  fornece funções e recursos para conexão MQTT
#include "lwip/apps/mqtt_priv.h"    // Biblioteca que fornece funções e recursos para Geração de Conexões
#include "lwip/dns.h"               // Biblioteca que fornece funções e recursos suporte DNS:
#include "lwip/altcp_tls.h"         // Biblioteca que fornece funções e recursos para conexões seguras usando TLS:

#include "hardware/i2c.h"           // Biblioteca para conexão I2C com SSD1306
#include "hardware/pwm.h"           // Pwm para sinal sonoro do buzzer ao passar de 7 setores energizados
#include "hardware/clocks.h"        // Usado no calculo do pwm do buzzer
#include "hardware/pio.h"           // Para controle do Pio
#include "hardware/gpio.h"          // Biblioteca de hardware de GPIO
#include "lib/leds.h"               // Definções para leds usados
#include "lib/font.h"               // Fonte usada para desenhar no Display oled
#include "lib/ssd1306.h"            // Bilioteca com funções utilitárias do display oled

#include "ws2812.pio.h"             // Arquivo .h gerado pelo Pio em Assembly



// -- DEFINES e Variáveis -------------------------------------------------------------------------------------------------------------------



#define WIFI_SSID "SEU SSID"         // Substitua pelo nome da sua rede Wi-Fi
#define WIFI_PASSWORD "SUA SENHA"   // Substitua pela senha da sua rede Wi-Fi
#define MQTT_SERVER "SEU MQTT BROKKER IP ADDRESS"    // Substitua pelo endereço do host - broket MQTT: Ex: 192.168.1.107
#define MQTT_USERNAME "Nome do host mqtt"     // Substitua pelo nome da host MQTT - Username
#define MQTT_PASSWORD "Senha do host do mqtt"     // Substitua pelo Password da host MQTT - credencial de acesso - caso exista


// Variáveis para conexão i2c e do display oled
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C
ssd1306_t ssd; // Inicializa a estrutura do display


// Para sinalizar se esta monitorando ou não energia, flag modificada por um botão no mqtt panel e sinalizada via um led rgb na protoboard
static volatile bool esta_monitorando_energia = false;
#define NIVEL_ENERGIA_MAXIMA 1000 // W pois cada setor pode ir de 0 a 125W
static volatile int nivel_energia_total = 0; // Somado novamente de todos os setores em cada mudança 

// Para controle do buzzer intermitente
static uint64_t ultima_troca_de_estado_buzzer_us = 0; // Guarda o último momento em que o buzzer mudou de estado
static bool buzzer_esta_ativo = false; // Estado atual do buzzer (ligado/desligado)
#define BUZZER_INTERVALO_ATIVAMENTO 200 // Intervalo de 200ms para cada ciclo (100ms ligado, 100ms desligado)
#define BUZZER_GPIO 10
uint32_t wrap = 6000;



// Variáveis estáticas globais para guardar o estado anterior do display 
static int ultima_qtd_setores_energizados_mostrados = -1; // -1 para garantir primeira atualização
static bool ultimo_estado_alerta = false; // True se o alerta/buzzer/LEDs brancos estavam ativos
static bool ultima_mensagem_alerta_ativa = false; // True se a mensagem de alerta estava no OLED
static bool ultimo_estado_desligamento_critico = false; // Adicione esta variável global/estática

// Para guardar a ultima porcentagem mostrada pela matriz e evitar flicks desnecessários por conta da frequência de 80hz do pio
static int ultima_porcentagem_mostrada = -1; // -1 para garantir a primeira atualização


// Temporização da coleta de tempo a cada 1 segundo para atualizar corretamente
#define TEMP_WORKER_TIME_S 1
// Variável estática para guardar o tempo anterior em segundos
static uint32_t ultimo_uptime_s_publicado = 0;

// Array de SectorState_t que guarda as infomações de cada setor como, pino gpio e nivel de energia
static volatile SectorState_t factory_sectors[8];


#ifndef MQTT_SERVER
#error Need to define MQTT_SERVER
#endif

// This file includes your client certificate for client server authentication
#ifdef MQTT_CERT_INC
#include MQTT_CERT_INC
#endif

#ifndef MQTT_TOPIC_LEN
#define MQTT_TOPIC_LEN 100
#endif

//Dados do cliente MQTT
typedef struct {
    mqtt_client_t* mqtt_client_inst;
    struct mqtt_connect_client_info_t mqtt_client_info;
    char data[MQTT_OUTPUT_RINGBUF_SIZE];
    char topic[MQTT_TOPIC_LEN];
    uint32_t len;
    ip_addr_t mqtt_server_address;
    bool connect_done;
    int subscribe_count;
    bool stop_client;
} MQTT_CLIENT_DATA_T;

#ifndef INFO_printf
#define INFO_printf printf
#endif

#ifndef ERROR_printf
#define ERROR_printf printf
#endif

// Manter o programa ativo - keep alive in seconds
#define MQTT_KEEP_ALIVE_S 60

// QoS - mqtt_subscribe
// QoS 0 (no máximo uma vez): Mais leve, mas pode resultar em perda de mensagens. É apropriado para dados não críticos, onde a perda ocasional é aceitáve
// QoS 1 (pelo menos uma vez):Garante que a mensagem seja entregue pelo menos uma vez, mas pode haver duplicações. É adequado para aplicações onde a entrega é importante, mas não é crítico que seja exatamente uma vez. 
// QoS 2 (exatamente uma vez):Mais robusta, garante que a mensagem seja entregue exatamente uma vez, sem duplicações. É indicada para aplicações onde a precisão e a integridade dos dados são essenciais. 
#define MQTT_SUBSCRIBE_QOS 0
#define MQTT_PUBLISH_QOS_0 0
#define MQTT_PUBLISH_QOS_1 1
#define MQTT_PUBLISH_QOS_2 2
#define MQTT_PUBLISH_RETAIN 0

// Tópico usado para: last will and testament
#define MQTT_WILL_TOPIC "/online"
#define MQTT_WILL_MSG "0"
#define MQTT_WILL_QOS 1

#ifndef MQTT_DEVICE_NAME
#define MQTT_DEVICE_NAME "pico"
#endif



// Definir como 1 para adicionar o nome do cliente aos tópicos, para suportar vários dispositivos que utilizam o mesmo servidor
#ifndef MQTT_UNIQUE_TOPIC
#define MQTT_UNIQUE_TOPIC 0
#endif



// Buffers para formar os leds na matriz de leds baseados na porcentagem do nivel de energia da fabrica


bool _0_porcento_de_energia[LED_COUNT] = {
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0
};



bool _20_porcento_de_energia[LED_COUNT] = {
    1, 1, 1, 1, 1,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0
};




bool _40_porcento_de_energia[LED_COUNT] = {
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0
};


bool _60_porcento_de_energia[LED_COUNT] = {
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0
};



bool _80_porcento_de_energia[LED_COUNT] = {
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    0, 0, 0, 0, 0
};

bool _100_porcento_de_energia[LED_COUNT] = {
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1
};


// --------------------------------------------------------------------------------------------------------------------------------------------








// -- Funções para controle do Hardware ------------------------------------------------------------------------------------------------------
#define BOTAOBOOTSEL 6 // Auto Explicativo
// Interrupção acionado pelo Botão B da BitDogLab para entrar em modo BootSel
void gpio_irq_handler(uint gpio, uint32_t events){
    reset_usb_boot(0, 0);
}



// Define um wrap igual ao payload maximo do slider do mqtt panel, para sincronização dos valores recebidos e transformação em niveis de brilho
void configure_led_pwm(int pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM); //habilitar o pino GPIO como PWM
    
    uint slice = pwm_gpio_to_slice_num(pin); //obter o canal PWM da GPIO
    
    pwm_set_clkdiv(slice, 4.0); //define o divisor de clock do PWM
    
    pwm_set_wrap(slice, 125); //definir o valor de wrap
    
    pwm_set_gpio_level(pin, 0); //definir o cico de trabalho (duty cycle) do pwm
    
    pwm_set_enabled(slice, true); //habilita o pwm no slice correspondente
}


// Somente inicializa gpios usadas para os leds e botão(da interrupção)
void InicializaGpios(void){
    gpio_init(BOTAOBOOTSEL);
    gpio_set_dir(BOTAOBOOTSEL, GPIO_IN);
    gpio_pull_up(BOTAOBOOTSEL);
    gpio_set_irq_enabled_with_callback(BOTAOBOOTSEL, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    
    for (int i = 0; i < 8; i++){
        configure_led_pwm(gpios_setores[i]); // Coloca os 8 setores como leds controlados por pwm
        factory_sectors[i].gpio_pin = gpios_setores[i]; // Armazena a gpio de cada setor
        factory_sectors[i].energia_atual = 0; // Começa desligado
    }
    gpio_init(GPIO_LED_MONITORAMENTO); // Setor que avisa que o monitoramente de energia esta funcionando
    gpio_set_dir(GPIO_LED_MONITORAMENTO, GPIO_OUT); // Ele não precisa de pwm como os outros
    // Leds usados para avisos(como o estado de alerta)
    gpio_init(GPIO_LED_AZUL_BITDOGLAB);
    gpio_set_dir(GPIO_LED_AZUL_BITDOGLAB,GPIO_OUT);
    gpio_init(GPIO_LED_VERDE_BITDOGLAB);
    gpio_set_dir(GPIO_LED_VERDE_BITDOGLAB,GPIO_OUT);
    gpio_init(GPIO_LED_VERMELHO_BITDOGLAB);
    gpio_set_dir(GPIO_LED_VERMELHO_BITDOGLAB,GPIO_OUT);
    
}



// Configurar pwm no pino do buzzer
void configure_buzzer(int pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM); // Define o pino 21 para função pwm
    uint slice_num = pwm_gpio_to_slice_num(pin); // Pega o slice correspondente a este pino
    pwm_config config = pwm_get_default_config(); // Peega um conjunto de valores padrões para a configuração do pwm
    pwm_set_wrap(slice_num, wrap - 1); // Define o wrap no slice correspondente
    uint32_t clock_hz = clock_get_hz(clk_sys); // Pega o clock do sistema que é 125Mhz
    uint32_t clkdiv = clock_hz / (440 * wrap); //  Calcula o divisor de clock
    pwm_set_clkdiv(slice_num, clkdiv); // Define o divisor de clock no slice correspondente
    pwm_init(slice_num, &config, true); // Inicializa o pwm naquele slice
    pwm_set_gpio_level(pin, 0); // Define o duty cycle pra 0
}


// Inicialização i2c , usando a 400Khz
void inicializaDisplay_I2C(void){
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
    gpio_pull_up(I2C_SDA);                                        // Pull up the data line
    gpio_pull_up(I2C_SCL);                                        // Pull up the clock line
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); // Inicializa o display
    ssd1306_config(&ssd);                                         // Configura o display
    ssd1306_fill(&ssd, false); // Limpa o display. O display inicia com todos os pixels apagados.
    ssd1306_send_data(&ssd); // Envia os dados
}

// Bloco Pio e state machine usadas na matriz de leds
void configura_Inicializa_Pio(void){
    PIO pio = pio0;// Seleciona o bloco pio que será usado
    int sm = 0; // Define qual state machine será usada
    uint offset = pio_add_program(pio, &ws2812_program);// Carrega o programa PIO para controlar os WS2812 na memória do PIO.
    ws2812_program_init(pio, sm, offset, MATRIZ_LED_PIN, 800000, false); //Inicializa a State Machine para executar o programa PIO carregado.
}



// Controle de brilho do LED por setor usando PWM
static void controla_energia_setor(int gpio_setor, uint8_t energia) {
    uint slice_num = pwm_gpio_to_slice_num(gpio_setor);
    uint channel_num = pwm_gpio_to_channel(gpio_setor);
    
    uint8_t pwm_level = energia;
    // Depuração
    // printf("Setor: %d, Slice: %d, Canal: %d, Nível PWM: %d\n",
    //    gpio_setor, slice_num, channel_num, pwm_level);
    
    pwm_set_chan_level(slice_num, channel_num, pwm_level);
    
    for (int i = 0; i < 8; i++) {
        if (factory_sectors[i].gpio_pin == gpio_setor) {
            factory_sectors[i].energia_atual = energia; // Guardar o valor de 0-125w
            break; // Encontrou e atualizou
        }
    }
    // Recalcula o nível de energia total somando todos os setores
    nivel_energia_total = 0;
    for (int i = 0; i < 8; i++) {
        nivel_energia_total += factory_sectors[i].energia_atual;
    }
}



void checaEstadoDeEnergiaDaFabrica(){
    // Garante que a contagem não seja negativa 
    if (quantidadeSetoresEnergizados < 0) {
        quantidadeSetoresEnergizados = 0;
    }

    // Define os estados do sistema 
    bool is_normal_range = nivel_energia_total <= 500;
    bool is_alert_range = nivel_energia_total > 500 && nivel_energia_total < NIVEL_ENERGIA_MAXIMA;
    bool is_critical_shutdown_range = (nivel_energia_total >= NIVEL_ENERGIA_MAXIMA); 
    
    // Determina se os indicadores de ALERTA (LEDs, Buzzer) devem estar ativos 
    bool alerta_deveria_estar_ativo = false;
    if ((esta_monitorando_energia && is_alert_range)) {
        alerta_deveria_estar_ativo = true;
    }

    // --- Controle dos LEDs da BitDogLab (Vermelho, Verde, Azul)
    if (alerta_deveria_estar_ativo != ultimo_estado_alerta) {
        if (alerta_deveria_estar_ativo) { // Se deve ativar o alerta
            gpio_put(GPIO_LED_AZUL_BITDOGLAB,1);
            gpio_put(GPIO_LED_VERDE_BITDOGLAB,1);
            gpio_put(GPIO_LED_VERMELHO_BITDOGLAB,1); // LEDs BitDogLab brancos
        } else { // Se deve desativar o alerta (voltar ao normal)
            gpio_put(GPIO_LED_AZUL_BITDOGLAB,0);
            gpio_put(GPIO_LED_VERDE_BITDOGLAB,0);
            gpio_put(GPIO_LED_VERMELHO_BITDOGLAB,0);
        }
        ultimo_estado_alerta = alerta_deveria_estar_ativo;
    }
    
    // --- Lógica de Bip Intermitente para o Buzzer 
    if (esta_monitorando_energia && is_alert_range ) {
        uint64_t current_time_us = time_us_64(); // Pega o tempo atual em microssegundos
        if (current_time_us - ultima_troca_de_estado_buzzer_us >= BUZZER_INTERVALO_ATIVAMENTO * 1000ULL) { // Se ja passou o tempo necessário
            if (buzzer_esta_ativo) { // Se o buzzer esta ativo, desative
                pwm_set_gpio_level(BUZZER_GPIO, 0);
                buzzer_esta_ativo = false;
            } else { // Senão ative
                pwm_set_gpio_level(BUZZER_GPIO, wrap/2);
                buzzer_esta_ativo = true;
            }
            ultima_troca_de_estado_buzzer_us = current_time_us; // Armazena o tempo da ultima troca de estado
        }
    } else {// Se não esta monitorando energia desative o buzzer
        if (buzzer_esta_ativo) {
            pwm_set_gpio_level(BUZZER_GPIO, 0);
            buzzer_esta_ativo = false;
        }
    }

    // --- Lógica Específica para o Estado Crítico/Desligamento Total (se monitorando) ---
    // Ações a serem executadas APENAS UMA VEZ ao ENTRAR no estado crítico
    if (esta_monitorando_energia && is_critical_shutdown_range && !ultimo_estado_desligamento_critico) {
        // Desliga todos os LEDs dos setores
        for (int i = 0; i < 8; i++){
            uint slice = pwm_gpio_to_slice_num(factory_sectors[i].gpio_pin);
            uint channel = pwm_gpio_to_channel(factory_sectors[i].gpio_pin);
            pwm_set_chan_level(slice, channel, 0); // Define brilho 0 para o LED
            factory_sectors[i].energia_atual = 0; // Atualiza o estado interno para 0
        }

        // Reseta as variáveis globais de energia e contagem de setores
        nivel_energia_total = 0;
        quantidadeSetoresEnergizados = 0;
        
        // Limpa o display OLED e força atualização
        ssd1306_fill(&ssd,false);
        ultima_mensagem_alerta_ativa = true; // Força uma atualização para garantir que o display seja limpo
        
        // Atualiza a flag de estado crítico para que esta lógica não seja executada novamente
        ultimo_estado_desligamento_critico = true;
    }
    // Se o sistema NÃO ESTÁ MAIS no estado crítico de desligamento (saiu do limite ou monitoramento desativado)
    else if (!is_critical_shutdown_range && ultimo_estado_desligamento_critico) {
        ultimo_estado_desligamento_critico = false; // Reseta a flag
    }

    
    // --- Controle do Display OLED (SSD1306) 
    bool display_changed = false;
    if (esta_monitorando_energia && is_alert_range) {// Se esta monitorando e esta no estado de alerta
        if (!ultima_mensagem_alerta_ativa) { // Caso não ja tenha mudado a mensagem para alerta
            ssd1306_draw_alert_message(&ssd); // desenhe a mensagem de alerta
            ultima_mensagem_alerta_ativa = true;
            display_changed = true;
        }
    } else { // Estado normal, ou monitoramento desativado
        if (ultima_mensagem_alerta_ativa || ultima_qtd_setores_energizados_mostrados != quantidadeSetoresEnergizados) {
            ssd1306_show_numbers_of_sectors_on(&ssd, quantidadeSetoresEnergizados);
            ultima_mensagem_alerta_ativa = false;
            display_changed = true;
        }
    }
    
    // Envia os dados para o display OLED APENAS se alguma mudança foi feita 
    if (display_changed || ultima_qtd_setores_energizados_mostrados == -1) { // Use ultima_qtd_setores_energizados_mostrados para o primeiro update
        ssd1306_send_data(&ssd);
    }

    // Atualiza o estado anterior do nível de energia total para o display (importante!)
    ultima_qtd_setores_energizados_mostrados = quantidadeSetoresEnergizados;
}



void mostra_porcentagem_de_energia_pela_matriz_de_leds(){
    // Converte o valor de energia total em porcentagem 
    float porcentagem = ( (float)nivel_energia_total / NIVEL_ENERGIA_MAXIMA ) * 100.0f; 
    
    // Somente para limitir caso ocorra algum erro inesperado
    if (porcentagem > 100.0f) {
        porcentagem = 100.0f;
    }
    if (porcentagem < 0.0f) {
        porcentagem = 0.0f;
    }

    int limite_atual_da_porcentagem;
    
    // Define em que limiar de energia o sistema se encontra
    if (porcentagem == 0.0f) { // Exactly 0%
        limite_atual_da_porcentagem = 0;
    } else if (porcentagem > 0.0f && porcentagem <= 20.0f) {
        limite_atual_da_porcentagem = 20;
    } else if (porcentagem > 20.0f && porcentagem <= 40.0f) { 
        limite_atual_da_porcentagem = 40;
    } else if (porcentagem > 40.0f && porcentagem <= 60.0f) { 
        limite_atual_da_porcentagem = 60;
    } else if (porcentagem > 60.0f && porcentagem <= 99.9f) { 
        limite_atual_da_porcentagem = 80;
    } else { 
        limite_atual_da_porcentagem = 100;
    }
    
    // Somente atualiza a matriz se o limiar de exibição mudou
    if (limite_atual_da_porcentagem != ultima_porcentagem_mostrada) {
        // Lógica para definir as cores e padrões dos LEDs baseados em 'limite_atual_da_porcentagem'
        if (limite_atual_da_porcentagem == 0){
            set_one_led(255,255,255,_0_porcento_de_energia); // Branco 
        }
        else if(limite_atual_da_porcentagem == 20){
            set_one_led(0,0,10,_20_porcento_de_energia); // Blue
        }
        else if (limite_atual_da_porcentagem == 40){
            set_one_led(0,0,10,_40_porcento_de_energia); // Blue
        }
        else if(limite_atual_da_porcentagem == 60){
            set_one_led(10,10,0,_60_porcento_de_energia); // Yellow
        }
        else if (limite_atual_da_porcentagem == 80 ){
            set_one_led(10,0,0,_80_porcento_de_energia); // Red
        }
        else{ // limite_atual_da_porcentagem == 100
            set_one_led(10,10,10,_100_porcento_de_energia); // White 
        }

        // Atualiza o último limiar exibido
        ultima_porcentagem_mostrada = limite_atual_da_porcentagem;
    }
}


// ------------------------------------------------------------------------------------------------------------------------------------------------

// -- Funções relacionadas a conexão e interação com o broker mqtt e envio de informações do cliente(rasp) ------------------------------------ 


// Callback para requisição de publicação
static void pub_request_cb(__unused void *arg, err_t err) {
    if (err != 0) {
        ERROR_printf("pub_request_cb failed %d", err);
    }
}




//Topico MQTT, sempre retorna somente o name neste código
static const char *full_topic(MQTT_CLIENT_DATA_T *state, const char *name) {
#if MQTT_UNIQUE_TOPIC
    static char full_topic[MQTT_TOPIC_LEN];
    snprintf(full_topic, sizeof(full_topic), "/%s%s", state->mqtt_client_info.client_id, name);
    return full_topic;
#else
    return name;
#endif
}


// Requisição de Assinatura - subscribe
static void sub_request_cb(void *arg, err_t err) {
    MQTT_CLIENT_DATA_T* state = (MQTT_CLIENT_DATA_T*)arg;
    if (err != 0) {
        ERROR_printf("subscribe request failed %d", err);
    }
    state->subscribe_count++; // aumenta o contador de assinatura
    //DEBUG
    printf("DEBUG: Subscription ACK received. Current subscribe_count: %d\n", state->subscribe_count);
}




// Requisição para encerrar a assinatura
static void unsub_request_cb(void *arg, err_t err) {
    MQTT_CLIENT_DATA_T* state = (MQTT_CLIENT_DATA_T*)arg;
    if (err != 0) {
        ERROR_printf("unsubscribe request failed %d", err);
    }
    state->subscribe_count--; // Diminui o contador de assinaturas
    assert(state->subscribe_count >= 0);

    // Stop if requested
    if (state->subscribe_count <= 0 && state->stop_client) {
        mqtt_disconnect(state->mqtt_client_inst);
    }
}



// Tópicos de assinatura
static void sub_unsub_topics(MQTT_CLIENT_DATA_T* state, bool sub) {
    mqtt_request_cb_t cb = sub ? sub_request_cb : unsub_request_cb;
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "energia/setor1"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "energia/setor2"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "energia/setor3"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "energia/setor4"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "energia/setor5"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "energia/setor6"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "energia/setor7"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "energia/setor8"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "monitoramento/energia"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "desligar/energia/setores"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "fabrica/uptime_s"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
}



// Dados recebidos pelos payloads mqtt, atraves do mqtt panel ou mqtt explorer
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags) {
    MQTT_CLIENT_DATA_T* state = (MQTT_CLIENT_DATA_T*)arg;
#if MQTT_UNIQUE_TOPIC
    const char *basic_topic = state->topic + strlen(state->mqtt_client_info.client_id) + 1;
#else
    const char *basic_topic = state->topic;
#endif
    strncpy(state->data, (const char *)data, len);
    state->len = len;
    state->data[len] = '\0';

    int received_value = -1; // Valor para o brilho ou comando

    // Tente converter o payload para inteiro
    if (len > 0 && state->data[0] != '\0') {
        received_value = atoi(state->data);
    }
    

    
    // --- Lógica para cada setor de 1 a 8
    for (int i = 0; i < 8; i++){
        char expetec_topic[MQTT_TOPIC_LEN];
        snprintf(expetec_topic,sizeof(expetec_topic), "energia/setor%d",i + 1);

        if (strcmp(basic_topic,expetec_topic) == 0){
            // Determia o estado anterior de 'ligado' para este setor
            bool was_on = factory_sectors[i].energia_atual > 0;
            // Controle o brilho do Led
            controla_energia_setor(factory_sectors[i].gpio_pin, (uint8_t) received_value);

            // Determi o novo estado de 'ligado' para este setor
            bool is_on = received_value > 0;

            // Atualiza a contagem se o estado de ligado/desligado mudou
            if (is_on && !was_on){// Se ligou
                quantidadeSetoresEnergizados++;
            }else if (!is_on && was_on){ // Se desligou
                quantidadeSetoresEnergizados--;
            }
        }
    }
    
    // --- Lógica para "desligar/energia/setores" ---
    if (strcmp(basic_topic, "desligar/energia/setores") == 0) {
        for (int i = 0; i < 8; i++){// Percorre todos os setores
            controla_energia_setor(factory_sectors[i].gpio_pin, 0); // Define brilho 0 para todos            
        }
        nivel_energia_total = 0;
        quantidadeSetoresEnergizados = 0;
    }
    else if (strcmp(basic_topic, "monitoramento/energia") == 0) {
        if (lwip_stricmp((const char *)state->data, "On") == 0 || strcmp((const char *)state->data, "1") == 0){
            gpio_put(GPIO_LED_MONITORAMENTO,1);
            esta_monitorando_energia = true;
        }
        else if (lwip_stricmp((const char *)state->data, "Off") == 0 || strcmp((const char *)state->data, "0") == 0){
            gpio_put(GPIO_LED_MONITORAMENTO,0);
            esta_monitorando_energia = false;
        }
    }
}




// Dados de entrada publicados
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len) {
    MQTT_CLIENT_DATA_T* state = (MQTT_CLIENT_DATA_T*)arg;
    strncpy(state->topic, topic, sizeof(state->topic));
    // Debug:
    printf("DEBUG: Incoming PUBLISH received for topic: '%s' (len: %d)\n", state->topic, tot_len);
}


// Publicar tempo
static void publish_time(MQTT_CLIENT_DATA_T *state) {
    const char *timer_key = full_topic(state, "fabrica/uptime_s");
    // Obtenha o tempo atual em microssegundos desde o boot
    uint64_t current_time_us = to_us_since_boot(get_absolute_time());
    // Converte para segundos
    uint32_t current_time_s = (uint32_t)(current_time_us / 1000000ULL);
    if (current_time_s != ultimo_uptime_s_publicado){
        ultimo_uptime_s_publicado = current_time_s;
        char time_str[20]; // Buffer maior para tempo em segundos/minutos/horas
        uint32_t hours = current_time_s / 3600;
        uint32_t minutes = (current_time_s % 3600) / 60;
        uint32_t seconds = current_time_s % 60;
        snprintf(time_str, sizeof(time_str), "%02lu:%02lu:%02lu", hours, minutes, seconds);
        INFO_printf("Publishing %s to %s\n", time_str, timer_key);
        mqtt_publish(state->mqtt_client_inst, timer_key, time_str, strlen(time_str), MQTT_PUBLISH_QOS_1, MQTT_PUBLISH_RETAIN, pub_request_cb, state);
    }
    
}


static void timer_worker_fn(async_context_t *context, async_at_time_worker_t *worker) {
    MQTT_CLIENT_DATA_T* state = (MQTT_CLIENT_DATA_T*)worker->user_data;
    publish_time(state);
    async_context_add_at_time_worker_in_ms(context, worker, TEMP_WORKER_TIME_S);
}    

static async_at_time_worker_t timer_worker = { .do_work = timer_worker_fn };



// Conexão MQTT
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    MQTT_CLIENT_DATA_T* state = (MQTT_CLIENT_DATA_T*)arg;
    if (status == MQTT_CONNECT_ACCEPTED) {
        state->connect_done = true;
        sub_unsub_topics(state, true); // subscribe;

        // indicate online
        if (state->mqtt_client_info.will_topic) {
            mqtt_publish(state->mqtt_client_inst, state->mqtt_client_info.will_topic, "1", 1, MQTT_WILL_QOS, true, pub_request_cb, state);
        }
        timer_worker.user_data = state;
        async_context_add_at_time_worker_in_ms(cyw43_arch_async_context(), &timer_worker, 0);

    } else if (status == MQTT_CONNECT_DISCONNECTED) {
        if (!state->connect_done) {
            ERROR_printf("Failed to connect to mqtt server");
        }
    }
    else {
        ERROR_printf("Unexpected status");
    }
}




// Inicializar o cliente MQTT
static void start_client(MQTT_CLIENT_DATA_T *state) {
#if LWIP_ALTCP && LWIP_ALTCP_TLS
    const int port = MQTT_TLS_PORT;
    INFO_printf("Using TLS\n");
#else
    const int port = MQTT_PORT;
    INFO_printf("Warning: Not using TLS\n");
#endif

    state->mqtt_client_inst = mqtt_client_new();
    if (!state->mqtt_client_inst) {
        ERROR_printf("MQTT client instance creation error");
    }
    INFO_printf("IP address of this device %s\n", ipaddr_ntoa(&(netif_list->ip_addr)));
    INFO_printf("Connecting to mqtt server at %s\n", ipaddr_ntoa(&state->mqtt_server_address));

    cyw43_arch_lwip_begin();
    if (mqtt_client_connect(state->mqtt_client_inst, &state->mqtt_server_address, port, mqtt_connection_cb, state, &state->mqtt_client_info) != ERR_OK) {
        ERROR_printf("MQTT broker connection error");
    }
#if LWIP_ALTCP && LWIP_ALTCP_TLS
    // This is important for MBEDTLS_SSL_SERVER_NAME_INDICATION
    mbedtls_ssl_set_hostname(altcp_tls_context(state->mqtt_client_inst->conn), MQTT_SERVER);
#endif
    mqtt_set_inpub_callback(state->mqtt_client_inst, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, state);
    cyw43_arch_lwip_end();
}



// Call back com o resultado do DNS
static void dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    MQTT_CLIENT_DATA_T *state = (MQTT_CLIENT_DATA_T*)arg;
    if (ipaddr) {
        state->mqtt_server_address = *ipaddr;
        start_client(state);
    } else {
        ERROR_printf("dns request failed");
    }
}

//-------------------------------------------------------------------------------------------------------------------------------------------

int main(void) {
    // Inicializa todos os tipos de bibliotecas stdio padrão presentes que estão ligados ao binário.
    stdio_init_all();
    sleep_ms(3000);
    InicializaGpios();

    // Inicializa display oled e conexão i2c
    inicializaDisplay_I2C();

    // Inicializa variáveis e programa PIO
    configura_Inicializa_Pio();

    //Configura o pwm no pino do Buzzer
    configure_buzzer(BUZZER_GPIO);

    INFO_printf("mqtt client starting\n");

    // Cria registro com os dados do cliente
    static MQTT_CLIENT_DATA_T state;

    // Inicializa a arquitetura do cyw43
    if (cyw43_arch_init()) {
        ERROR_printf("Failed to inizialize CYW43");
    }


    // Usa identificador único da placa
    char unique_id_buf[5];
    pico_get_unique_board_id_string(unique_id_buf, sizeof(unique_id_buf));
    for(int i=0; i < sizeof(unique_id_buf) - 1; i++) {
        unique_id_buf[i] = tolower(unique_id_buf[i]);
    }

    // Gera nome único, Ex: pico1234
    char client_id_buf[sizeof(MQTT_DEVICE_NAME) + sizeof(unique_id_buf) - 1];
    memcpy(&client_id_buf[0], MQTT_DEVICE_NAME, sizeof(MQTT_DEVICE_NAME) - 1);
    memcpy(&client_id_buf[sizeof(MQTT_DEVICE_NAME) - 1], unique_id_buf, sizeof(unique_id_buf) - 1);
    client_id_buf[sizeof(client_id_buf) - 1] = 0;
    INFO_printf("Device name %s\n", client_id_buf);

    state.mqtt_client_info.client_id = client_id_buf;
    state.mqtt_client_info.keep_alive = 300; // Keep alive in sec
#if defined(MQTT_USERNAME) && defined(MQTT_PASSWORD)
    state.mqtt_client_info.client_user = MQTT_USERNAME;
    state.mqtt_client_info.client_pass = MQTT_PASSWORD;
#else
    state.mqtt_client_info.client_user = NULL;
    state.mqtt_client_info.client_pass = NULL;
#endif
    static char will_topic[MQTT_TOPIC_LEN];
    strncpy(will_topic, full_topic(&state, MQTT_WILL_TOPIC), sizeof(will_topic));
    state.mqtt_client_info.will_topic = will_topic;
    state.mqtt_client_info.will_msg = MQTT_WILL_MSG;
    state.mqtt_client_info.will_qos = MQTT_WILL_QOS;
    state.mqtt_client_info.will_retain = true;
//
#if LWIP_ALTCP && LWIP_ALTCP_TLS
    // TLS enabled
#ifdef MQTT_CERT_INC
    static const uint8_t ca_cert[] = TLS_ROOT_CERT;
    static const uint8_t client_key[] = TLS_CLIENT_KEY;
    static const uint8_t client_cert[] = TLS_CLIENT_CERT;
    // This confirms the indentity of the server and the client
    state.mqtt_client_info.tls_config = altcp_tls_create_config_client_2wayauth(ca_cert, sizeof(ca_cert),
            client_key, sizeof(client_key), NULL, 0, client_cert, sizeof(client_cert));
#if ALTCP_MBEDTLS_AUTHMODE != MBEDTLS_SSL_VERIFY_REQUIRED
    WARN_printf("Warning: tls without verification is insecure\n");
#endif
#else
    state->client_info.tls_config = altcp_tls_create_config_client(NULL, 0);
    WARN_printf("Warning: tls without a certificate is insecure\n");
#endif
#endif
//
    // Conectar à rede WiFI - fazer um loop até que esteja conectado
    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 60000)) {
        ERROR_printf("Failed to connect");
    }
    INFO_printf("\nConnected to Wifi\n");

    //Faz um pedido de DNS para o endereço IP do servidor MQTT
    cyw43_arch_lwip_begin();
    int err = dns_gethostbyname(MQTT_SERVER, &state.mqtt_server_address, dns_found, &state);
    cyw43_arch_lwip_end();

    // Se tiver o endereço, inicia o cliente
    if (err == ERR_OK) {
        start_client(&state);
    } else if (err != ERR_INPROGRESS) { // ERR_INPROGRESS means expect a callback
        ERROR_printf("dns request failed");
    }

    // Loop condicionado a conexão mqtt
    while (!state.connect_done || mqtt_client_is_connected(state.mqtt_client_inst)) {
        cyw43_arch_poll();
        checaEstadoDeEnergiaDaFabrica();
        mostra_porcentagem_de_energia_pela_matriz_de_leds();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(100));
    }

    INFO_printf("mqtt client exiting\n");
    return 0;
}

