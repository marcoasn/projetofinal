
//Criar uma solução IoT, utilizando o protocolo MQTT, da mesma forma que a tarefa anterior.
// O ESP32 deve permanecer em modo deep sleep até que algum evento ocorra, acordando-o. 
//Após acordá-lo, informações de sensores ou status são trocadas via MQTT e o ESP32 volta 
//para o estado deep sleep.
//Aluno: Marco Antonio Saraiva do Nascimento



#include "rom/ets_sys.h"

//#include "waiter.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "ultrasonic.h"


#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASS ""
#define BLINK_GPIO 2
#define BLINK_GPIO_TASK2 4
#define TRIGGER_GPIO 18
#define ECHO_GPIO 5
#define TRIGGER_GPIO2 13
#define ECHO_GPIO2 12
#define TRIGGER_GPIO3 15
#define ECHO_GPIO3 19
#define TRIGGER_GPIO4 23
#define ECHO_GPIO4 21
#define ext_wakeup_pin_0 14
#define GPIO_PIN (14)
#define GPIO_PIN2 (22)
#define MAX_DISTANCE_CM 500 // 5m max
#define LONG_TIME 0xffff

static uint8_t s_led_state = 0;
RTC_DATA_ATTR int portao_block;
SemaphoreHandle_t xSemaphore = NULL;
esp_netif_t * my_ap;
char liga[3]="on2";
char desliga[4]="off2";
char liga4[3]="on4";
char desliga4[4]="off4";
bool dist, dist2, dist3, dist4; 
RTC_DATA_ATTR int lotado=0;
bool x=0;
int counter, countermqtt;
xQueueHandle interputQueue;
bool inscrito;
int leitura_feita, envio_feito, abrir_cancela;
 char att[26]="Leds acesos(GPIO's):";
esp_mqtt_client_handle_t client = NULL;
int countmqtterro=0;
void wifi_connection(void);
bool conexaomqtt =0;
bool botao_pressionado_dormindo=0;

static void wifi_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void mqtt_app_start(void);
void Publisher_Task(void *params);
void ultrasonic_test(void *pvParameters);
void dormir_test(void *pvParameters);
void cfg_sleep();
static const char *TAG = "DHT";
TaskHandle_t myTaskHandle2 = NULL;


//interrupção causada pelo botao ser pressionado
static void IRAM_ATTR funcPtr(void* arg)
{
uint32_t gpio_num = (uint32_t) arg;
xQueueSendFromISR(interputQueue, &gpio_num, NULL);
}

void app_main(void)
{
    nvs_flash_init();  // Flash memory initiation
    xTaskCreate(&ultrasonic_test, "ultrasonic_test", 2048, NULL, 0, NULL);
    //cfg do botão
    interputQueue = xQueueCreate(1, sizeof(int));
    gpio_num_t pin = GPIO_PIN;
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    io_conf.pin_bit_mask = (0x1 << GPIO_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    gpio_config(&io_conf);
    gpio_set_intr_type(pin, GPIO_INTR_POSEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(pin, funcPtr, (void*) pin);

    wifi_connection();
    
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_PIN2, GPIO_MODE_OUTPUT);
    gpio_set_direction(BLINK_GPIO_TASK2, GPIO_MODE_OUTPUT);
    leitura_feita=0;
    envio_feito=0;
    int pinNumber, count = 0;
    xSemaphore = xSemaphoreCreateBinary();
    //checar causa do esp ter acordado
    esp_sleep_wakeup_cause_t causa;
    causa=esp_sleep_get_wakeup_cause();
    switch(causa) {
    case ESP_SLEEP_WAKEUP_EXT0 : botao_pressionado_dormindo=1;
    }
    
    xTaskCreate(&Publisher_Task, "led_potenciometro", 2048, "Task 1", 0, NULL);
	
    if(botao_pressionado_dormindo==1 && lotado==0){
         
            abrir_cancela=1;
            
            printf("gpio%d triggered\n", pinNumber);
            
            gpio_set_level(GPIO_PIN2, 1);
            vTaskDelay( 2000 / portTICK_RATE_MS );
            gpio_set_level(GPIO_PIN2, 0);
            abrir_cancela=0;
            
    }



    while(true){
        //rotina de abrir o portão caso o botão seja pressionado
        if (xQueueReceive(interputQueue, &pinNumber, portMAX_DELAY) && portao_block==0 && lotado==0)
        {
            abrir_cancela=1;
            
            printf("gpio%d triggered\n", pinNumber);
            
            gpio_set_level(GPIO_PIN2, 1);
            vTaskDelay( 2000 / portTICK_RATE_MS );
            gpio_set_level(GPIO_PIN2, 0);
            abrir_cancela=0;
            }
        //}
        
    }
}



//leitura dos sensores ultrassônicos. caso detectem algo a menos de 3,7m a vaga é marcada como ocupada
void ultrasonic_test(void *pvParameters)
{
    ultrasonic_sensor_t sensor = {
        .trigger_pin = TRIGGER_GPIO,
        .echo_pin = ECHO_GPIO
    };
    ultrasonic_sensor_t sensor2 = {
        .trigger_pin = TRIGGER_GPIO2,
        .echo_pin = ECHO_GPIO2
    };
    ultrasonic_sensor_t sensor3 = {
        .trigger_pin = TRIGGER_GPIO3,
        .echo_pin = ECHO_GPIO3
    };
     ultrasonic_sensor_t sensor4 = {
        .trigger_pin = TRIGGER_GPIO4,
        .echo_pin = ECHO_GPIO4
    };
    ultrasonic_init(&sensor);
    ultrasonic_init(&sensor2);
    ultrasonic_init(&sensor3);
    ultrasonic_init(&sensor4);
    while (true)
    {
        float distance;
        float distance2;
        float distance3;
        float distance4;
        esp_err_t res = ultrasonic_measure(&sensor, MAX_DISTANCE_CM, &distance);
        esp_err_t res2 = ultrasonic_measure(&sensor2, MAX_DISTANCE_CM, &distance2);
        esp_err_t res3 = ultrasonic_measure(&sensor3, MAX_DISTANCE_CM, &distance3);
        esp_err_t res4 = ultrasonic_measure(&sensor4, MAX_DISTANCE_CM, &distance4);
        if (res != ESP_OK)
        {
            printf("Error %d: ", res);
            switch (res)
            {
                case ESP_ERR_ULTRASONIC_PING:
                    printf("Cannot ping (device is in invalid state)\n");
                    break;
                case ESP_ERR_ULTRASONIC_PING_TIMEOUT:
                    printf("Ping timeout (no device found)\n");
                    break;
                case ESP_ERR_ULTRASONIC_ECHO_TIMEOUT:
                    printf("Echo timeout (i.e. distance too big)\n");
                    break;
                default:
                    printf("%s\n", esp_err_to_name(res));
            }
        }
        else
            printf("Distance: %0.04f cm\n", distance*100);
            printf("Distance2: %0.04f cm\n", distance2*100);
            printf("Distance3: %0.04f cm\n", distance3*100);
            printf("Distance4: %0.04f cm\n", distance4*100);
            if(distance<3.7){
                dist=1;
            }
            else{
                dist=0;
            }
            if(distance2<3.7){
                dist2=1;
            }
            else{
                dist2=0;
            }
            if(distance3<3.7){
                dist3=1;
            }
            else{
                dist3=0;
            }
            if(distance4<3.7){
                dist4=1;
            }
            else{
                dist4=0;
            }
        leitura_feita=1;
        if(dist2==1 && dist==1 && dist3==1 && dist4==1){
            lotado=1;
        } 
        else{
            lotado=0;
        }
        vTaskDelay(pdMS_TO_TICKS(6000));
    }
}



//configuração do modo deepsleep
void cfg_sleep(){
    const int wakeup_time_sec = 20;
    printf("Enabling timer wakeup, %ds\n", wakeup_time_sec);
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(wakeup_time_sec * 1000000));
    printf("Enabling EXT0 wakeup on pin GPIO%d\n", ext_wakeup_pin_0);
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(ext_wakeup_pin_0, 1));
     esp_deep_sleep_start();
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        printf("Connected and subscribing... ");
        msg_id = esp_mqtt_client_subscribe(client, "controle", 2);
        msg_id = esp_mqtt_client_subscribe(client, "atualizar", 2);
        printf("OK! \n");
        conexaomqtt=1;
        break;
    case MQTT_EVENT_DISCONNECTED:
        countmqtterro++;
        if(counter==3){
            esp_restart();
        }
        break;
    case MQTT_EVENT_SUBSCRIBED:
        msg_id = esp_mqtt_client_publish(client, "controle", "Funcionou hein?", 0, 0, 0);
        inscrito=1;
        //msg_id = esp_mqtt_client_publish(client, "ANDRE", "OPA", 0, 0, 0);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        break;
    case MQTT_EVENT_PUBLISHED:
        break;
    case MQTT_EVENT_DATA:
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        
        if(strncmp(event->topic,"atualizar", event->topic_len) == 0){
        envio_feito=1;
        }
        if(strncmp(event->data, desliga , event->data_len) == 0){
        gpio_set_level(BLINK_GPIO, 0);
        s_led_state=!s_led_state;
        }
        //liga desliga o led do pino gpio 4
        if(strncmp(event->data,liga4, event->data_len) == 0){
        //gpio_set_level(BLINK_GPIO_TASK2, 1);
        printf("chegou");
        portao_block=1;
        printf("estad %d\n", portao_block);
        }
         if(strncmp(event->data, desliga4 , event->data_len) == 0){
        //gpio_set_level(BLINK_GPIO_TASK2, 0);
        printf("chegou");
        portao_block=0;
        }
        break;
    case MQTT_EVENT_ERROR:
    countmqtterro++;
    if(counter==3){
            esp_restart();
        }
        break;
    default:
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = "mqtt://test.mosquitto.org",
        .disable_clean_session = 1,
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    printf("Starting MQTT... \n");
    esp_mqtt_client_start(client);
    
}

void wifi_connection(void)
{
    
  
    esp_netif_init();                       // TCP/IP initiation
    esp_event_loop_create_default();        // Event Loop
    my_ap =esp_netif_create_default_wifi_sta();    // WiFi Station
     

    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_initiation);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
 
    wifi_config_t wifi_configuration = {
    .sta = {
      .ssid = WIFI_SSID,
      .password = WIFI_PASS,
    },
  };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_configuration);

    esp_wifi_start();

    esp_wifi_connect();

    x=1;
    vTaskDelay(3000/portTICK_PERIOD_MS);
}

static void wifi_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        printf("Wifi connecting....\n");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        printf("Wifi connected!\n");
        vTaskDelay(3000/portTICK_PERIOD_MS);
        mqtt_app_start();
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        printf("Wifi disconnected!\n");

        esp_wifi_connect();
        counter++;
        if(counter==3){
            esp_restart();
        }

        break;
    case IP_EVENT_STA_GOT_IP:
        printf("Wifi got IP!\n\n");
        break;                    
    default:
        printf("EVENT ID: %d\n", event_id);
        break;
    }
}

//publicar informações sobre as vagas
void Publisher_Task(void *params)
{
    int pinNumber, count = 0;
  while (true)
  {
      
    if(conexaomqtt==1)
    {
        
       
        //esp_mqtt_client_publish(client, "atualizar", att, 0, 0, 0);
        if(dist==1){
            esp_mqtt_client_publish(client, "atualizar", "VAGA OCUPADA", 0, 0, 0);
            
        }
        else{
            esp_mqtt_client_publish(client, "atualizar", "VAGA LIVRE", 0, 0, 0);
            
        }
        if(dist2==1){
            esp_mqtt_client_publish(client, "atualizar2", "VAGA OCUPADA", 0, 0, 0);
            
        }
        else{
            esp_mqtt_client_publish(client, "atualizar2", "VAGA LIVRE", 0, 0, 0);
            
        }
        if(dist3==1){
            esp_mqtt_client_publish(client, "atualizar3", "VAGA OCUPADA", 0, 0, 0);
            
        }
        else{
            esp_mqtt_client_publish(client, "atualizar3", "VAGA LIVRE", 0, 0, 0);
            
        }
        if(dist4==1){
            esp_mqtt_client_publish(client, "atualizar4", "VAGA OCUPADA", 0, 0, 0);
            
        }
        else{
            esp_mqtt_client_publish(client, "atualizar4", "VAGA LIVRE", 0, 0, 0);
            
        }
      
        strcpy(att, "Leds acesos(GPIO's):");
        
        
     }

        //condições para que o esp32 possa ir para o deep sleep
         if(leitura_feita==1 && envio_feito==1 && abrir_cancela==0 && inscrito==1){
           
            cfg_sleep();
        }
        if(leitura_feita==1 && envio_feito==0){
            
            
        }  
        vTaskDelay(500 / portTICK_PERIOD_MS);

    
  }
}



