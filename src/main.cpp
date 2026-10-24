#include <Arduino.h>
#include <driver/twai.h>

// change to whatever GPIO pin you want
#define TX_PIN GPIO_NUM_5
#define RX_PIN GPIO_NUM_4

// Update with actual CAN baud rate
#define TIMING_CONFIG TWAI_TIMING_CONFIG_500KBITS

#define SERIAL_BAUD 115200

#define CAN_FILTER_LENGTH 64
#define CAN_FILTER_EMPTY (1U << 31)

uint32_t can_id_filter[CAN_FILTER_LENGTH] = {CAN_FILTER_EMPTY};

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
        Serial.printf("Err: ESP_ERR %lx '%s'\r\n", status, esp_err_to_name(status));
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

esp_err_t transmit_message(char *input)
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

    while ((c = input[index++]) && c != ' ')
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
    while ((c = input[index++]))
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

bool try_command(char **input, const char *prefix)
{
    // See if input starts with prefix. If so, return true and advance
    // *input until the char after the space. Otherwise, return false and
    // do not advance. Do not pass GO!

    size_t prefix_length = strlen(prefix);

    if (strncmp(*input, prefix, prefix_length) != 0)
        return false;

    *input += prefix_length;
    return true;
}

void set_filter(char *params)
{
    while (*params == ' ')
        params++;

    if (strcmp(params, "clear") == 0)
    {
        Serial.println("Filter: Filter cleared.");
        can_id_filter[0] = CAN_FILTER_EMPTY;
        return;
    }
    else if (strncmp(params, "add", 3) == 0)
    {
        params += 3;

        while (*params == ' ')
            params++;

        uint32_t can_id = 0;
        char c;
        while ((c = *(params++)) && c != ' ')
        {
            uint8_t nibble_or_death = char_to_nibble(c);

            if (nibble_or_death >> 4)
            {
                Serial.printf("Err: Can't parse hex digit '%c'\r\n", c);
                return;
            }

            can_id <<= 4;
            can_id |= nibble_or_death;
        }

        Serial.printf("Filter: Adding %lx to CAN id filter\r\n", can_id);

        for (int i = 0; i < CAN_FILTER_LENGTH; i++)
        {
            bool last = i == CAN_FILTER_LENGTH - 1;

            if (can_id_filter[i] != CAN_FILTER_EMPTY)
            {
                if (last)
                    Serial.println("Err: Filter list full");
                continue;
            }

            can_id_filter[i] = can_id;
            if (!last)
                can_id_filter[i + 1] = CAN_FILTER_EMPTY;
            break;
        }
    }
}

void submit_command(char *input)
{
    if (try_command(&input, "filter"))
    {
        set_filter(input);
        return;
    }

    Serial.printf("Send: '%s'\r\n", input);
    esp_err_t status = transmit_message(input);
    log_error(status);
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
    Serial.println("Send a CAN packet with '<can id> <can data>', with both formatted as hex.");
    Serial.println("Try 'filter add <can id>' or 'filter clear' to filter what messages you recieve.");
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

        // If empty, let everyone in
        bool found = can_id_filter[0] == CAN_FILTER_EMPTY;

        for (int i = 0; i < CAN_FILTER_LENGTH; i++)
        {
            if (can_id_filter[i] == CAN_FILTER_EMPTY)
                break;

            if (can_id_filter[i] != message.identifier)
                continue;

            found = true;
            break;
        }

        if (!found)
            continue;

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

            submit_command(input_buffer);
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