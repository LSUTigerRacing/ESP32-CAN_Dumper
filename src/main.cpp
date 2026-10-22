#include <Arduino.h>
#include <driver/twai.h>

// change to whatever GPIO pin you want
#define TX_PIN GPIO_NUM_5
#define RX_PIN GPIO_NUM_4

// Update with actual CAN baud rate
#define TIMING_CONFIG TWAI_TIMING_CONFIG_500KBITS

#define SERIAL_BAUD 115200

[[noreturn]] void fatal(const char *why)
{
    // please remember to send \r\n everywhere because some terminals are stupid
    Serial.printf("FATAL Err: %s\r\n", why);

    while (true)
    {
        delay(1000);
    }
}

void log_error(esp_err_t status)
{
    switch (status)
    {
    case ESP_OK:
        break;

    case ESP_ERR_TIMEOUT:
        Serial.println("Err: Timeout");
        break;

    default:
        Serial.printf("Err: ESP_ERR %x '%s'\r\n", status, esp_err_to_name(status));
    }
}

uint8_t char_to_nibble(char c)
{
    // top 4 bits set for errors

    // EVIL LOWERCASE
    if (c >= 'A' && c <= 'F')
        c = 'a' + (c - 'A');

    if (c >= 'a' && c <= 'f')
    {
        return 0xA + (c - 'a');
    }
    else if (c >= '0' && c <= '9')
    {
        // LEAP OF FAITH: Assume 0 before 1. Probs is
        return c - '0';
    }

    return 0b11110000;
}

esp_err_t transmit_message(char *input_string)
{
    // Messages are input as "XXXXXX YYYYYYYYYYYY" where X's are the ID and those Y's are
    // up to TWAI_FRAME_MAX_DLC bytes... there is some extension allowed in weird
    // protocols but lets not support that for now hehe. We'll see if anything the
    // device touches needs it

    twai_message_t message = {
        .flags = TWAI_MSG_FLAG_NONE};

    // 11 or 29 bit depending on standard
    int index = 0;
    char c;

    while ((c = input_string[index++]) && c != ' ')
    {
        uint8_t nibble_or_death = char_to_nibble(c);

        if (nibble_or_death >> 4)
        {
            Serial.printf("Err: Can't parse hex digit '%c'\r\n", c);
            return ESP_ERR_INVALID_ARG;
        }

        message.identifier <<= 4;
        message.identifier |= nibble_or_death;
    }

    // hack for ID without data
    if (c == '\0')
        index--;

    if (message.identifier >> 11)
        message.flags = TWAI_MSG_FLAG_EXTD;

    int data_index = 0;
    bool lower = false;
    while ((c = input_string[index++]))
    {
        if (data_index >= TWAI_FRAME_MAX_DLC)
        {
            Serial.printf("Err: CAN data exceeds max of 8 bytes\r\n");
            return ESP_ERR_INVALID_ARG;
        }

        uint8_t nibble_or_death = char_to_nibble(c);
        if (nibble_or_death >> 4)
        {
            Serial.printf("Err: Can't parse hex digit '%c'\r\n", c);
            return ESP_ERR_INVALID_ARG;
        }

        message.data[data_index] |= nibble_or_death << (lower ? 0 : 4);

        if (lower)
            data_index++;
        lower = !lower;
    }

    if (lower)
    {
        data_index++;
        Serial.printf("Warn: Padding last byte. Pls gimmie even bytes\r\n");
    }

    message.data_length_code = data_index;

    return twai_transmit(&message, pdMS_TO_TICKS(50));
}

void setup()
{
    Serial.begin(SERIAL_BAUD);

    // no ack to not like inject nonsense vomit into the cars bus
    twai_general_config_t gen_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_PIN, RX_PIN, TWAI_MODE_NO_ACK);

    // idk how fast actual data comes in but just to be safe
    gen_config.rx_queue_len = 64;

    twai_timing_config_t timing_config = TIMING_CONFIG();
    twai_filter_config_t filter_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&gen_config, &timing_config, &filter_config) != ESP_OK)
        fatal("TWAI driver unable to install");

    if (twai_start() != ESP_OK)
        fatal("TWAI driver unable to start");

    Serial.println("Yay! Ready!");
    Serial.println("Send a CAN packet with '<can id> <can data>', with both formatted as hex");
}

void loop()
{
    twai_status_info_t status = {};
    while (twai_get_status_info(&status) == ESP_OK && status.msgs_to_rx > 0)
    {
        twai_message_t message = {};

        // FUTURE: Should we be waiting at all?
        esp_err_t read_status = twai_receive(&message, pdMS_TO_TICKS(15));

        if (read_status != ESP_OK)
        {
            log_error(read_status);
            continue;
        }

        Serial.printf("Recv: [%lx] ' ", message.identifier);
        for (int i = 0; i < message.data_length_code; i++)
        {
            Serial.printf("%02x ", message.data[i]);
        }
        Serial.print("'\r\n");
    }

    static char input_buffer[128] = {};
    const size_t buffer_len = sizeof(input_buffer) / sizeof(char);

    while (Serial.available() > 0)
    {
        // Maybe a little gross
        size_t buffer_idx = strlen(input_buffer);

        char c = Serial.read();

        if (c == '\n' || c == '\r')
        {
            if (buffer_idx == 0)
                continue;

            Serial.print("\r\n");
            Serial.printf("Send: '%s'\r\n", input_buffer);

            esp_err_t status = transmit_message(input_buffer);
            log_error(status);
            input_buffer[0] = '\0';
            continue;
        }

        // lotsa terminal emulators dont do echoing
        Serial.print(c);

        if (buffer_idx >= buffer_len - 1)
            continue;

        input_buffer[buffer_idx] = c;
        input_buffer[buffer_idx + 1] = '\0';
    }
}