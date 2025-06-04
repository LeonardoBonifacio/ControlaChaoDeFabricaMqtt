
# Sistema de Monitoramento Inteligente de energia para Fábrica com Raspberry Pi Pico W

Este projeto implementa um sistema de monitoramento inteligente para uma fábrica, utilizando o **Raspberry Pi Pico W**. Ele integra:

- **Conectividade Wi-Fi** via MQTT para comunicação.
- **LEDs PWM** para simular o consumo de energia de setores.
- **Display OLED** para feedback visual.
- **Buzzer** para alertas sonoros.
- **Matriz de LEDs WS2812** para uma representação visual do consumo total.

---

## Funcionalidades Principais

### Monitoramento de Setores (LEDs PWM)
- Oito LEDs (simulando setores da fábrica) são controlados individualmente via **PWM**.
- O brilho de cada LED representa o nível de energia consumido por aquele setor (**0-125W por setor**).

### Comunicação MQTT
- O Pico W conecta-se a um **broker MQTT**, publicando seu **tempo de atividade (uptime)**.
- Inscreve-se em tópicos para controlar:
  - O nível de energia de cada setor.
  - Ativar/desativar o modo de monitoramento.

### Cálculo de Energia Total
- O sistema calcula o **consumo total da fábrica** (`nivel_energia_total`), com um **limite máximo de 1000W**.

### Alertas Inteligentes
- **LEDs de Alerta (BitDogLab):** indicam o estado da fábrica (normal, alerta, crítico) com cores específicas.
- **Buzzer intermitente:** bipes de 200ms (100ms ligado, 100ms desligado) quando o sistema entra em estado de alerta.

### Desligamento Automático
- Se o `nivel_energia_total` atingir `NIVEL_ENERGIA_MAXIMA` (**1000W**), todos os LEDs dos setores são desligados automaticamente e as variáveis de energia são resetadas.

### Display OLED (SSD1306)
- Exibe informações importantes como o **nível de energia total** ou **mensagens de alerta**, fornecendo feedback local.

### Matriz de LEDs (WS2812)
- Representa visualmente o **consumo percentual de energia total** da fábrica (0%, 20%, 40%, 60%, 80%, 100%).
- A atualização ocorre **apenas quando o limiar de porcentagem muda**, evitando flicker.


## Hardware Necessário

Para montar este projeto, você precisará dos seguintes componentes:

- 1x Raspberry Pi Pico W
- 9x LEDs (8 comuns e  1 módulo RGB de duas pernas)
- Resistores: adequados para os LEDs (ex: 220-330 ohms para 3.3V)
- 1x Display OLED SSD1306 (128x64 ou similar, com interface I2C)
- 1x Buzzer (passivo ou ativo, compatível com PWM)
- 1x Matriz de LEDs WS2812 (NeoPixel ou similar, pelo menos 25 LEDs para uma matriz 5x5)
- 1x Protoboard e fios jumper
- 1x Botão tátil (para o modo bootsel)
- Fonte de alimentação USB (para o Pico W)
- Um **broker MQTT** (como Mosquitto) rodando em sua rede local, configurado pelo termux pelo celular por exemplo.
- Um **cliente MQTT** (ex: MQTT Explorer) para testar a comunicação.
- Um **cliente MQTT** (ex: MQTT Panel) para controle do sistema.

---

## Configuração do Hardware (Pinagem)

| Componente | GPIO do Pico W | Observações |
|------------|----------------|-------------|
| LEDs dos Setores | 16, 17, 28, 18, 19, 20, 8, 9 | Todos configurados com PWM |
| LED de Monitoramento | 4 | Saída digital |
| LEDs de Alerta (BitDogLab) | 11 (Azul), 12 (Verde), 13 (Vermelho) | Saídas digitais para alertas |
| Buzzer | 10 | Conectado a um slice PWM diferente do GPIO 20 |
| Display OLED (I2C) | 14 (SDA), 15 (SCL) | Interface I2C (i2c1) |
| Matriz de LEDs WS2812 | 7 | Controlado via PIO |

---

## Configuração e Compilação do Software

### 1. Instale o SDK do Raspberry Pi Pico
Certifique-se de ter o **Pico SDK** configurado em seu ambiente. Consulte a [documentação oficial](https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-c-sdk.pdf) para detalhes.

### 2. Clone o Repositório do Projeto

```bash
git clone [URL_DO_SEU_REPOSITORIO]
cd [nome_da_pasta_do_projeto]
```

### 3. Ajuste as Credenciais Wi-Fi e MQTT

Abra o arquivo `main.c` e modifique:

```c
#define WIFI_SSID "SUA_REDE_WIFI"
#define WIFI_PASSWORD "SUA_SENHA_WIFI"
#define MQTT_SERVER "IP_DO_SEU_BROKER_MQTT" // Ex: "192.168.1.107"
#define MQTT_USERNAME "SEU_USUARIO_MQTT"    // Opcional
#define MQTT_PASSWORD "SUA_SENHA_MQTT"      // Opcional
```

### 4. Crie o Diretório de Build e Execute o CMake

```bash
mkdir build
cd build
cmake ..
```

### 5. Compile o Projeto

```bash
make
```

O arquivo `.uf2` será gerado na pasta `build` (ex: `pico_factory_monitor.uf2`).

---

## Como Fazer Upload para o Raspberry Pi Pico W

1. **Desconecte o Pico W** se estiver conectado.
2. **Entre no Modo Bootloader:** pressione e mantenha o botão `BOOTSEL` (ou o botão customizado na **GPIO 6**) enquanto conecta o cabo USB.
3. O Pico W aparecerá como unidade USB (`RPI-RP2`).
4. **Arraste o arquivo `.uf2`** gerado para essa unidade.
5. O Pico W reiniciará automaticamente e executará o novo firmware.

---

## Tópicos MQTT Utilizados

### Tópicos de Inscrição (Controle)

- `energia/setor1` a `energia/setor8`: envie valor numérico (`uint8_t` de 0 a 125) para ajustar nível de energia e brilho.
- `monitoramento/energia`: envie `"On"` (ou `"1"`) para ativar o monitoramento; `"Off"` (ou `"0"`) para desativar.
- `desligar/energia/setores`: envie qualquer mensagem para forçar desligamento de todos os setores.

### Tópicos de Publicação (Status)

- `fabrica/uptime_s`: publica o tempo de funcionamento (`HH:MM:SS`).
- `/online`: tópico **LWT** que publica `"1"` quando o dispositivo conecta e `"0"` se desconectar inesperadamente.

---

## Estrutura do Código

O projeto está organizado em `main.c` e algumas bibliotecas (`lib/`). Funções principais:

- `InicializaGpios()`: configura os GPIOs.
- `configure_led_pwm()`: configura os LEDs com PWM.
- `configure_buzzer()`: configura PWM para o buzzer.
- `inicializaDisplay_I2C()`: inicializa a comunicação I2C com o display OLED.
- `configura_Inicializa_Pio()`: configura a PIO para controlar a matriz WS2812.
- `controla_energia_setor()`: ajusta brilho via PWM e recalcula `nivel_energia_total`.
- `checaEstadoDeEnergiaDaFabrica()`:
  - Define estado (normal, alerta, crítico).
  - Controla LEDs de alerta e buzzer.
  - Garante desligamento automático ao atingir `NIVEL_ENERGIA_MAXIMA`.
  - Atualiza o display OLED.
- `mostra_porcentagem_de_energia_pela_matriz_de_leds()`: converte `nivel_energia_total` em porcentagem e atualiza a matriz WS2812, evitando flicker.
- **Funções MQTT**:
  - `mqtt_connection_cb`
  - `mqtt_incoming_publish_cb`
  - `mqtt_incoming_data_cb`
  - `sub_unsub_topics`
  - `publish_time`
  - `dns_found`
  - `timer_worker_fn`

- `main()`: função principal que inicializa o hardware, conecta à Wi-Fi/MQTT e executa o loop.

---

## Notas de Depuração e Boas Práticas

- **Conflitos de Slice PWM:** evite configurar PWM em pinos que compartilham o mesmo slice (ex: GPIO 20 e 21).
- **Cálculo de Porcentagem:** ao dividir inteiros, faça `cast` para `float` para evitar truncamento.
- **Controle de Tempo Não Bloqueante:** use `time_us_64()` para efeitos intermitentes; evite `sleep_ms()`.
- **Otimização de Atualização:** atualize displays e matrizes **somente quando o conteúdo mudar**, evitando flicker e melhorando a performance.
