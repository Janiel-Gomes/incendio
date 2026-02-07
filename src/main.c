#include "FreeRTOS.h"
#include "hardware/adc.h" // Bibliotecas para temperatura interna
#include "hardware/i2c.h"
#include "hardware/sync.h"
#include "hx711.h"
#include "lwip/apps/http_client.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "queue.h"
#include "semphr.h"
#include "ssd1306.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

// ================= CONFIGURAÇÕES (ATUALIZE AQUI) =================
#define WIFI_SSID ""               // <-- Substitua pelo seu SSID
#define WIFI_PASSWORD ""           // <-- Substitua pela sua senha
#define SERVER_IP "192.168.143.29" // <-- IP do seu servidor local
#define SERVER_PORT 5000

#define LED_EXTERNO_PIN 11
#define BOTAO_A_PIN 5
#define FLAME_SENSOR_PIN 20 // Alterado para GP20 conforme solicitado
#define BUZZER_PIN 21       // Pino do Buzzer (comum em placas EmbarcaTech)
#define HX711_DATA_PIN 18
#define HX711_SCLK_PIN 19

// Globais
SemaphoreHandle_t xSemaforoBotao;
QueueHandle_t xFilaContador;
static uint32_t ultimo_tempo_botao = 0;
static volatile bool requisicao_em_curso = false;

// ====== FreeRTOS Static Memory ======
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize) {
  static StaticTask_t xIdleTaskTCB;
  static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
  *ppxIdleTaskStackBuffer = uxIdleTaskStack;
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize) {
  static StaticTask_t xTimerTaskTCB;
  static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
  *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
  *ppxTimerTaskStackBuffer = uxTimerTaskStack;
  *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

// ===================== CALLBACK HTTP =====================
static void http_client_callback(void *arg, httpc_result_t httpc_result,
                                 u32_t rx_content_len, u32_t srv_res,
                                 err_t err) {
  requisicao_em_curso = false;
  if (httpc_result == HTTPC_RESULT_OK) {
    printf("HTTP: Sucesso! Status: %d\n", (int)srv_res);
  } else {
    printf("HTTP: Falha LwIP %d\n", (int)httpc_result);
  }
}

// ===================== CALLBACK RECEPÇÃO (NOVO) =====================
static err_t http_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p,
                                err_t err) {
  if (p != NULL) {
    // Notifica o LwIP que os dados foram processados (descartados neste caso)
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
  }
  return ERR_OK;
}

// ===================== TASK: ENVIO HTTP (MODIFICADA) =====================
void http_post_task(void *pvParameters) {
  ip_addr_t server_addr;
  ip4addr_aton(SERVER_IP, &server_addr);

  adc_init();
  adc_set_temp_sensor_enabled(true);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));

    if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP) {
      if (requisicao_em_curso) {
        // Watchdog: se demorar mais de 10s, destrava
        static int timeout_watchdog = 0;
        if (++timeout_watchdog > 2) {
          requisicao_em_curso = false;
          timeout_watchdog = 0;
        }
        continue;
      }

      // Lê sensores
      adc_select_input(4);
      uint16_t raw = adc_read();
      float voltage = raw * 3.3f / 4096.0f;
      float temp = 27.0f - (voltage - 0.706f) / 0.001721f;
      int flama = gpio_get(FLAME_SENSOR_PIN);

      requisicao_em_curso = true;
      httpc_connection_t settings;
      memset(&settings, 0, sizeof(settings));
      settings.result_fn = http_client_callback;

      char uri[128];
      snprintf(uri, sizeof(uri), "/update?temp=%.2f&flame=%d&device=pico_w",
               temp, flama);

      printf("HTTP: Tentando conectar em %s:%d...\n", SERVER_IP, SERVER_PORT);

      err_t err = httpc_get_file(&server_addr, SERVER_PORT, uri, &settings,
                                 http_recv_callback, NULL, NULL);

      if (err != ERR_OK) {
        printf("LwIP: Erro ao iniciar conexao (%d)\n", (int)err);
        requisicao_em_curso = false;
      }
    }
  }
}

// ===================== OUTRAS TASKS =====================

void botao_irq_handler(uint gpio, uint32_t events) {
  uint32_t tempo_atual = to_ms_since_boot(get_absolute_time());
  if (tempo_atual - ultimo_tempo_botao > 250) {
    ultimo_tempo_botao = tempo_atual;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xSemaforoBotao, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}

void botao_task(void *pvParameters) {
  gpio_init(BOTAO_A_PIN);
  gpio_set_dir(BOTAO_A_PIN, GPIO_IN);
  gpio_pull_up(BOTAO_A_PIN);
  gpio_set_irq_enabled_with_callback(BOTAO_A_PIN, GPIO_IRQ_EDGE_FALL, true,
                                     &botao_irq_handler);
  while (true) {
    if (xSemaphoreTake(xSemaforoBotao, portMAX_DELAY) == pdPASS) {
      printf("Botao A pressionado!\n");
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  }
}

void led_interno_task(void *pvParameters) {
  if (cyw43_arch_init()) {
    vTaskDelete(NULL);
  }
  cyw43_arch_enable_sta_mode();
  if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                         CYW43_AUTH_WPA2_AES_PSK, 30000)) {
    printf("Falha ao conectar no Wi-Fi :(\n");
    vTaskDelete(NULL);
  }
  printf("Wi-Fi Conectado! IP da Pico: %s\n",
         ip4addr_ntoa(netif_ip4_addr(netif_default)));
  while (true) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void led_externo_task(void *pvParameters) {
  gpio_init(LED_EXTERNO_PIN);
  gpio_set_dir(LED_EXTERNO_PIN, GPIO_OUT);
  while (true) {
    gpio_put(LED_EXTERNO_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_put(LED_EXTERNO_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void sensor_flama_task(void *pvParameters) {
  printf("Iniciando tarefa do sensor de chama no GP%d...\n", FLAME_SENSOR_PIN);
  gpio_init(FLAME_SENSOR_PIN);
  gpio_set_dir(FLAME_SENSOR_PIN, GPIO_IN);
  gpio_pull_up(FLAME_SENSOR_PIN);

  gpio_init(BUZZER_PIN);
  gpio_set_dir(BUZZER_PIN, GPIO_OUT);
  gpio_put(BUZZER_PIN, 0);

  int leituras_consecutivas = 0;

  while (true) {
    int estado = gpio_get(FLAME_SENSOR_PIN);

    if (estado == 0) {
      leituras_consecutivas++;
    } else {
      leituras_consecutivas = 0;
      gpio_put(BUZZER_PIN, 0);
    }

    // Se o sensor estiver "travado" em 0, vamos avisar
    if (leituras_consecutivas > 50) {
      printf("AVISO: Sensor parece travado em 0. Verifique se o LED do sensor "
             "apaga ao girar o parafuso.\n");
      leituras_consecutivas = 5; // Evita spam infinito
    }

    // Alerta apenas se detectar fogo por 5 vezes seguidas
    if (leituras_consecutivas >= 5) {
      printf("\n*******************************\n");
      printf("  ALERTA: FOGO DETECTADO!  \n");
      printf("*******************************\n\n");

      gpio_put(BUZZER_PIN, 1);
      vTaskDelay(pdMS_TO_TICKS(150));
      gpio_put(BUZZER_PIN, 0);

      if (leituras_consecutivas > 20)
        leituras_consecutivas = 5;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void oled_task(void *pvParameters) {
  i2c_init(i2c1, 400000);
  gpio_set_function(14, GPIO_FUNC_I2C);
  gpio_set_function(15, GPIO_FUNC_I2C);
  gpio_pull_up(14);
  gpio_pull_up(15);
  ssd1306_t disp;
  disp.external_vcc = false;
  ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
  int contador = 0;
  char buffer[20];
  while (true) {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 1, "Monitoramento");

    snprintf(buffer, sizeof(buffer), "Cont: %d", contador);
    ssd1306_draw_string(&disp, 0, 16, 1, buffer);

    // Mostra o status do sensor de chama no OLED
    if (gpio_get(FLAME_SENSOR_PIN) == 0) {
      ssd1306_draw_string(&disp, 0, 32, 2, "FOGO!!!");
    } else {
      ssd1306_draw_string(&disp, 0, 32, 1, "Sist: OK");
    }

    ssd1306_show(&disp);

    xQueueOverwrite(xFilaContador, &contador);

    contador++;
    if (contador > 10)
      contador = 0;
    vTaskDelay(pdMS_TO_TICKS(1000)); // Atualiza a cada 1 segundo
  }
}

// ===================== MAIN =====================
int main() {
  stdio_init_all();
  xSemaforoBotao = xSemaphoreCreateBinary();
  xFilaContador = xQueueCreate(1, sizeof(int));

  // Aumentamos a Stack para 4096 e prioridade 2 para garantir fluidez
  xTaskCreate(http_post_task, "HTTP_Task", 4096, NULL, 2, NULL);

  xTaskCreate(botao_task, "Botao_Task", 512, NULL, 2, NULL);
  xTaskCreate(led_interno_task, "WiFi_Task", 1024, NULL, 1, NULL);
  xTaskCreate(led_externo_task, "LED_Ext_Task", 256, NULL, 1, NULL);
  xTaskCreate(sensor_flama_task, "Flame_Task", 1024, NULL, 1, NULL);
  xTaskCreate(oled_task, "OLED_Task", 1024, NULL, 1, NULL);

  vTaskStartScheduler();
  while (1) {
  }
}