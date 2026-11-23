
#include "main.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Function prototypes */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);

void OLED_Show(char *text);
void GUI_Send(char *msg);

void AS608_ClearRx(void);
uint8_t AS608_CheckResponseHeader(void);
uint8_t AS608_Command(uint8_t *command,
                      uint16_t command_len,
                      uint8_t *response,
                      uint16_t response_len,
                      uint32_t timeout);

uint8_t AS608_WaitFinger(void);
uint8_t AS608_WaitFingerRemove(void);
uint8_t AS608_GenChar1_Function(void);
uint8_t AS608_GenChar2_Function(void);
uint8_t AS608_RegModel_Function(void);
uint8_t AS608_StoreChar(uint16_t id);
void AddFingerprint(uint16_t id);
uint8_t AS608_Delete(uint16_t id);
uint8_t AS608_SearchFingerprint(uint16_t *found_id);
uint8_t AS608_ReadIndexTable(uint8_t page, uint8_t *index_table);
void AS608_ListFingerprints(void);
void AutoScan(void);
void AS608_RecoverUART1(void);
void UART2_Test1000(void);

/* =========================================================
 * HANDLE
 * ========================================================= */

I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;


/* =========================================================
 * AS608 BUFFER
 * ========================================================= */

uint8_t RxBuffer[32];

/* UART2 */
uint8_t rx2_byte;

char gui_cmd[32];

volatile uint8_t cmd_ready = 0;
volatile uint8_t cmd_idx = 0;

/*
 * UART2 state
 *
 * 1 = đang nhận interrupt
 * 0 = chưa nhận / cần khôi phục
 */
volatile uint8_t uart2_rx_active = 0;
volatile uint8_t uart2_recover_pending = 0;


/* =========================================================
 * AS608 COMMAND
 * ========================================================= */

/*
 * GenImg
 *
 * EF 01 FF FF FF FF 01 00 03 01 00 05
 */
uint8_t AS608_GetImage[] =
{
    0xEF, 0x01,
    0xFF, 0xFF, 0xFF, 0xFF,
    0x01,
    0x00, 0x03,
    0x01,
    0x00, 0x05
};


/*
 * GenChar - Buffer 1
 */
uint8_t AS608_GenChar1[] =
{
    0xEF, 0x01,
    0xFF, 0xFF, 0xFF, 0xFF,
    0x01,
    0x00, 0x04,
    0x02,
    0x01,
    0x00, 0x08
};


/*
 * GenChar - Buffer 2
 */
uint8_t AS608_GenChar2[] =
{
    0xEF, 0x01,
    0xFF, 0xFF, 0xFF, 0xFF,
    0x01,
    0x00, 0x04,
    0x02,
    0x02,
    0x00, 0x09
};


/*
 * RegModel
 */
uint8_t AS608_RegModel[] =
{
    0xEF, 0x01,
    0xFF, 0xFF, 0xFF, 0xFF,
    0x01,
    0x00, 0x03,
    0x05,
    0x00, 0x09
};


/*
 * Search Buffer 1
 */
uint8_t AS608_Search[] =
{
    0xEF, 0x01,
    0xFF, 0xFF, 0xFF, 0xFF,
    0x01,
    0x00, 0x08,
    0x04,
    0x01,
    0x00, 0x00,
    0x00, 0x7F,
    0x00, 0x8D
};


/* =========================================================
 * OLED
 * Giữ nguyên driver SSD1306 đang chạy của bạn
 * ========================================================= */

void OLED_Show(char *text)
{
    ssd1306_Fill(Black);

    ssd1306_SetCursor(0, 20);

    ssd1306_WriteString(
        text,
        Font_7x10,
        White
    );

    ssd1306_UpdateScreen();
}


/* =========================================================
 * GUI C#
 * ========================================================= */

void GUI_Send(char *msg)
{
    HAL_UART_Transmit(
        &huart2,
        (uint8_t *)msg,
        strlen(msg),
        500
    );
}


/* =========================================================
 * XÓA BUFFER UART1
 * ========================================================= */

void AS608_ClearRx(void)
{
    uint8_t dummy;

    __HAL_UART_CLEAR_OREFLAG(&huart1);

    while (
        HAL_UART_Receive(
            &huart1,
            &dummy,
            1,
            1
        ) == HAL_OK
    )
    {
    }

    memset(
        RxBuffer,
        0,
        sizeof(RxBuffer)
    );
}


/* =========================================================
 * KIỂM TRA HEADER RESPONSE
 * ========================================================= */

uint8_t AS608_CheckResponseHeader(void)
{
    if (RxBuffer[0] != 0xEF)
        return 0;

    if (RxBuffer[1] != 0x01)
        return 0;

    if (RxBuffer[6] != 0x07)
        return 0;

    return 1;
}


/* =========================================================
 * GỬI COMMAND + NHẬN RESPONSE
 * ========================================================= */

uint8_t AS608_Command(
    uint8_t *command,
    uint16_t command_len,
    uint8_t *response,
    uint16_t response_len,
    uint32_t timeout
)
{
    AS608_ClearRx();

    if (
        HAL_UART_Transmit(
            &huart1,
            command,
            command_len,
            500
        ) != HAL_OK
    )
    {
        return 0;
    }

    if (
        HAL_UART_Receive(
            &huart1,
            response,
            response_len,
            timeout
        ) != HAL_OK
    )
    {
        return 0;
    }

    return 1;
}


/* =========================================================
 * CHỜ ĐẶT NGÓN TAY
 *
 * Poll GenImg cho đến khi có ảnh.
 * Timeout 20 giây để không treo vô hạn.
 * ========================================================= */

uint8_t AS608_WaitFinger(void)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < 20000)
    {
        memset(
            RxBuffer,
            0,
            sizeof(RxBuffer)
        );

        /*
         * GenImg
         */
        if (
            HAL_UART_Transmit(
                &huart1,
                AS608_GetImage,
                sizeof(AS608_GetImage),
                200
            ) != HAL_OK
        )
        {
            HAL_Delay(100);
            continue;
        }

        /*
         * AS608 trả 12 byte.
         * Timeout 500 ms, ổn định hơn bản 80 ms.
         */
        if (
            HAL_UART_Receive(
                &huart1,
                RxBuffer,
                12,
                500
            ) == HAL_OK
        )
        {
            if (
                RxBuffer[0] == 0xEF &&
                RxBuffer[1] == 0x01 &&
                RxBuffer[6] == 0x07
            )
            {
                /*
                 * 0x00 = lấy được ảnh
                 */
                if (RxBuffer[9] == 0x00)
                {
                    return 1;
                }

                /*
                 * 0x02 = chưa có ngón.
                 * Tiếp tục polling.
                 */
                if (RxBuffer[9] == 0x02)
                {
                    HAL_Delay(100);
                    continue;
                }

                /*
                 * Lỗi khác.
                 */
                char err[32];

                sprintf(
                    err,
                    "GETIMG_ERR:%02X\n",
                    RxBuffer[9]
                );

                GUI_Send(err);

                HAL_Delay(100);
            }
        }
        else
        {
            /*
             * Timeout UART1 không được coi là treo.
             * Thử lại vòng kế tiếp.
             */
            HAL_Delay(100);
        }
    }

    GUI_Send("FINGER_TIMEOUT\n");

    return 0;
}


/* =========================================================
 * CHỜ NHẤC TAY
 *
 * Chờ đến khi AS608 xác nhận không còn ngón tay.
 * Timeout 8 giây để không khóa chương trình vô hạn.
 * ========================================================= */

uint8_t AS608_WaitFingerRemove(void)
{
    uint32_t start = HAL_GetTick();

    while (1)
    {
        memset(
            RxBuffer,
            0,
            sizeof(RxBuffer)
        );

        if (
            HAL_UART_Transmit(
                &huart1,
                AS608_GetImage,
                sizeof(AS608_GetImage),
                200
            ) != HAL_OK
        )
        {
            HAL_Delay(50);
            continue;
        }

        /*
         * 300 ms đủ để AS608 trả packet.
         */
        if (
            HAL_UART_Receive(
                &huart1,
                RxBuffer,
                12,
                300
            ) == HAL_OK
        )
        {
            if (
                RxBuffer[0] == 0xEF &&
                RxBuffer[1] == 0x01 &&
                RxBuffer[6] == 0x07 &&
                RxBuffer[9] == 0x02
            )
            {
                /*
                 * Xóa mọi byte còn sót lại trước khi
                 * chuyển sang thao tác tiếp theo.
                 */
                HAL_Delay(50);
                AS608_ClearRx();
                return 1;
            }
        }

        if ((HAL_GetTick() - start) >= 8000)
        {
            AS608_ClearRx();
            return 0;
        }

        HAL_Delay(50);
    }
}


/* =========================================================
 * GENCHAR BUFFER 1
 * ========================================================= */

uint8_t AS608_GenChar1_Function(void)
{
    if (
        !AS608_Command(
            AS608_GenChar1,
            sizeof(AS608_GenChar1),
            RxBuffer,
            12,
            1000
        )
    )
    {
        GUI_Send("GENCHAR1_TIMEOUT\n");
        return 0;
    }

    if (!AS608_CheckResponseHeader())
    {
        GUI_Send("GENCHAR1_PACKET_ERR\n");
        return 0;
    }

    if (RxBuffer[9] != 0x00)
    {
        char err[32];

        sprintf(
            err,
            "GENCHAR1_ERR:%02X\n",
            RxBuffer[9]
        );

        GUI_Send(err);

        return 0;
    }

    return 1;
}


/* =========================================================
 * GENCHAR BUFFER 2
 * ========================================================= */

uint8_t AS608_GenChar2_Function(void)
{
    if (
        !AS608_Command(
            AS608_GenChar2,
            sizeof(AS608_GenChar2),
            RxBuffer,
            12,
            1000
        )
    )
    {
        GUI_Send("GENCHAR2_TIMEOUT\n");
        return 0;
    }

    if (!AS608_CheckResponseHeader())
    {
        GUI_Send("GENCHAR2_PACKET_ERR\n");
        return 0;
    }

    if (RxBuffer[9] != 0x00)
    {
        char err[32];

        sprintf(
            err,
            "GENCHAR2_ERR:%02X\n",
            RxBuffer[9]
        );

        GUI_Send(err);

        return 0;
    }

    return 1;
}


/* =========================================================
 * REG MODEL
 * ========================================================= */

uint8_t AS608_RegModel_Function(void)
{
    if (
        !AS608_Command(
            AS608_RegModel,
            sizeof(AS608_RegModel),
            RxBuffer,
            12,
            1000
        )
    )
    {
        GUI_Send("REGMODEL_TIMEOUT\n");
        return 0;
    }

    if (!AS608_CheckResponseHeader())
    {
        GUI_Send("REGMODEL_PACKET_ERR\n");
        return 0;
    }

    if (RxBuffer[9] != 0x00)
    {
        char err[32];

        sprintf(
            err,
            "REGMODEL_ERR:%02X\n",
            RxBuffer[9]
        );

        GUI_Send(err);

        return 0;
    }

    return 1;
}


/* =========================================================
 * STORECHAR
 * ========================================================= */

uint8_t AS608_StoreChar(uint16_t id)
{
    uint8_t StoreChar[15];

    StoreChar[0] = 0xEF;
    StoreChar[1] = 0x01;

    StoreChar[2] = 0xFF;
    StoreChar[3] = 0xFF;
    StoreChar[4] = 0xFF;
    StoreChar[5] = 0xFF;

    StoreChar[6] = 0x01;

    StoreChar[7] = 0x00;
    StoreChar[8] = 0x06;

    StoreChar[9] = 0x06;

    StoreChar[10] = 0x01;

    StoreChar[11] =
        (id >> 8) & 0xFF;

    StoreChar[12] =
        id & 0xFF;

    uint16_t checksum =
        0x01 +
        0x00 +
        0x06 +
        0x06 +
        0x01 +
        StoreChar[11] +
        StoreChar[12];

    StoreChar[13] =
        (checksum >> 8) & 0xFF;

    StoreChar[14] =
        checksum & 0xFF;

    AS608_ClearRx();

    if (
        HAL_UART_Transmit(
            &huart1,
            StoreChar,
            15,
            500
        ) != HAL_OK
    )
    {
        GUI_Send("STORE_TX_ERR\n");
        return 0;
    }

    if (
        HAL_UART_Receive(
            &huart1,
            RxBuffer,
            12,
            1500
        ) != HAL_OK
    )
    {
        GUI_Send("STORE_TIMEOUT\n");
        return 0;
    }

    if (
        RxBuffer[0] != 0xEF ||
        RxBuffer[1] != 0x01 ||
        RxBuffer[6] != 0x07
    )
    {
        GUI_Send("STORE_PACKET_ERR\n");
        return 0;
    }

    if (RxBuffer[9] == 0x00)
    {
        return 1;
    }

    char err[32];

    sprintf(
        err,
        "STORE_ERR:%02X\n",
        RxBuffer[9]
    );

    GUI_Send(err);

    return 0;
}


/* =========================================================
 * THÊM VÂN TAY
 * ========================================================= */

void AddFingerprint(uint16_t id)
{
    /*
     * Đảm bảo UART1 sạch trước khi bắt đầu.
     */
    AS608_RecoverUART1();

    OLED_Show("DAT TAY LAN 1");
    GUI_Send("PLACE_FINGER_1\n");

    if (!AS608_WaitFinger())
    {
        OLED_Show("LOI DOC VAN TAY");
        GUI_Send("ADD_ERR\n");

        AS608_RecoverUART1();

        HAL_Delay(800);

        return;
    }

    OLED_Show("DANG XU LY LAN 1");

    if (!AS608_GenChar1_Function())
    {
        OLED_Show("LOI GENCHAR 1");
        GUI_Send("ADD_ERR\n");

        AS608_RecoverUART1();

        HAL_Delay(1000);

        return;
    }

    OLED_Show("NHAC TAY RA");
    GUI_Send("REMOVE_FINGER\n");

    /*
     * Phải nhấc ngón trước mẫu thứ 2.
     */
    AS608_WaitFingerRemove();

    HAL_Delay(100);

    OLED_Show("DAT TAY LAN 2");
    GUI_Send("PLACE_FINGER_2\n");

    if (!AS608_WaitFinger())
    {
        OLED_Show("LOI DOC VAN TAY");
        GUI_Send("ADD_ERR\n");

        AS608_RecoverUART1();

        HAL_Delay(800);

        return;
    }

    OLED_Show("DANG XU LY LAN 2");

    if (!AS608_GenChar2_Function())
    {
        OLED_Show("LOI GENCHAR 2");
        GUI_Send("ADD_ERR\n");

        AS608_RecoverUART1();

        HAL_Delay(1000);

        return;
    }

    OLED_Show("GHEP MAU");

    if (!AS608_RegModel_Function())
    {
        OLED_Show("LOI GHEP MAU");
        GUI_Send("ADD_ERR\n");

        AS608_RecoverUART1();

        HAL_Delay(1000);

        return;
    }

    OLED_Show("DANG LUU...");

    if (AS608_StoreChar(id))
    {
        OLED_Show("THEM THANH CONG");
        GUI_Send("ADD_OK\n");
    }
    else
    {
        OLED_Show("LOI LUU VAN TAY");
        GUI_Send("ADD_ERR\n");
    }

    /*
     * Quan trọng:
     * sau khi StoreChar xong thì reset USART1.
     * Điều này giúp lần ADD tiếp theo không bị giữ
     * ở trạng thái BUSY/packet cũ.
     */
    HAL_Delay(300);

    AS608_RecoverUART1();

    /*
     * Không để lần ADD sau bị ảnh hưởng bởi ngón
     * vẫn còn trên cảm biến.
     */
    OLED_Show("NHAC TAY RA");

    AS608_WaitFingerRemove();

    AS608_RecoverUART1();

    HAL_Delay(200);
}


/* =========================================================
 * DELETE FINGERPRINT
 * ========================================================= */

uint8_t AS608_Delete(uint16_t id)
{
    uint8_t DelChar[16];

    DelChar[0] = 0xEF;
    DelChar[1] = 0x01;

    DelChar[2] = 0xFF;
    DelChar[3] = 0xFF;
    DelChar[4] = 0xFF;
    DelChar[5] = 0xFF;

    DelChar[6] = 0x01;

    DelChar[7] = 0x00;
    DelChar[8] = 0x07;

    DelChar[9] = 0x0C;

    DelChar[10] =
        (id >> 8) & 0xFF;

    DelChar[11] =
        id & 0xFF;

    DelChar[12] = 0x00;
    DelChar[13] = 0x01;

    uint16_t checksum =
        0x01 +
        0x00 +
        0x07 +
        0x0C +
        DelChar[10] +
        DelChar[11] +
        0x00 +
        0x01;

    DelChar[14] =
        (checksum >> 8) & 0xFF;

    DelChar[15] =
        checksum & 0xFF;

    if (
        !AS608_Command(
            DelChar,
            16,
            RxBuffer,
            12,
            1500
        )
    )
    {
        return 0;
    }

    if (
        RxBuffer[0] == 0xEF &&
        RxBuffer[1] == 0x01 &&
        RxBuffer[6] == 0x07 &&
        RxBuffer[9] == 0x00
    )
    {
        return 1;
    }

    return 0;
}


/* =========================================================
 * SEARCH FINGERPRINT
 * ========================================================= */

uint8_t AS608_SearchFingerprint(
    uint16_t *found_id
)
{
    if (
        !AS608_Command(
            AS608_Search,
            sizeof(AS608_Search),
            RxBuffer,
            16,
            1500
        )
    )
    {
        return 0;
    }

    if (
        RxBuffer[0] != 0xEF ||
        RxBuffer[1] != 0x01 ||
        RxBuffer[6] != 0x07
    )
    {
        return 0;
    }

    if (RxBuffer[9] == 0x00)
    {
        *found_id =
            ((uint16_t)RxBuffer[10] << 8)
            | RxBuffer[11];

        return 1;
    }

    return 0;
}


/* =========================================================
 * READ INDEX TABLE
 *
 * Lệnh 0x1F - đọc bảng chỉ mục mẫu vân tay.
 *
 * Page:
 *   0 -> ID 0..255
 *   1 -> ID 256..511
 *   ...
 *
 * AS608 trả về:
 *   44 byte:
 *   EF 01 + ADDR(4) + 07 + 00 23
 *   + CONFIRM(1)
 *   + INDEX TABLE(32)
 *   + CHECKSUM(2)
 *
 * Mỗi byte trong INDEX TABLE chứa 8 ID.
 * Bit 7 ứng với ID đầu tiên của byte,
 * bit 0 ứng với ID cuối cùng.
 * ========================================================= */

uint8_t AS608_ReadIndexTable(
    uint8_t page,
    uint8_t *index_table
)
{
    uint8_t ReadIndexCmd[13];

    /*
     * EF 01
     */
    ReadIndexCmd[0] = 0xEF;
    ReadIndexCmd[1] = 0x01;

    /*
     * Address = FF FF FF FF
     */
    ReadIndexCmd[2] = 0xFF;
    ReadIndexCmd[3] = 0xFF;
    ReadIndexCmd[4] = 0xFF;
    ReadIndexCmd[5] = 0xFF;

    /*
     * Command packet
     */
    ReadIndexCmd[6] = 0x01;

    /*
     * Length = 0004
     * Instruction + Page + checksum(2)
     */
    ReadIndexCmd[7] = 0x00;
    ReadIndexCmd[8] = 0x04;

    /*
     * ReadIndexTable
     */
    ReadIndexCmd[9] = 0x1F;

    /*
     * Page
     */
    ReadIndexCmd[10] = page;

    /*
     * Checksum:
     * 01 + 00 + 04 + 1F + page
     */
    uint16_t checksum =
        0x01 +
        0x00 +
        0x04 +
        0x1F +
        page;

    ReadIndexCmd[11] =
        (checksum >> 8) & 0xFF;

    ReadIndexCmd[12] =
        checksum & 0xFF;

    /*
     * Response = 44 byte.
     */
    uint8_t response[44];

    memset(
        response,
        0,
        sizeof(response)
    );

    if (
        !AS608_Command(
            ReadIndexCmd,
            sizeof(ReadIndexCmd),
            response,
            sizeof(response),
            1500
        )
    )
    {
        return 0;
    }

    /*
     * Kiểm tra packet.
     */
    if (
        response[0] != 0xEF ||
        response[1] != 0x01 ||
        response[6] != 0x07 ||
        response[7] != 0x00 ||
        response[8] != 0x23
    )
    {
        return 0;
    }

    /*
     * CONFIRM:
     * 0x00 = đọc thành công
     */
    if (response[9] != 0x00)
    {
        return 0;
    }

    /*
     * 32 byte index table nằm từ response[10]
     * đến response[41].
     */
    memcpy(
        index_table,
        &response[10],
        32
    );

    return 1;
}


/* =========================================================
 * LIST FINGERPRINTS
 *
 * Chỉ liệt kê ID 1..127 vì vùng Search hiện tại
 * của chương trình cũng đang dùng 0..127.
 *
 * STM32 gửi:
 *
 *   LIST,1,2,5,7
 *
 * hoặc nếu không có mẫu:
 *
 *   LIST,EMPTY
 *
 * Nếu lỗi:
 *
 *   LIST_ERR
 * ========================================================= */

void AS608_ListFingerprints(void)
{
    uint8_t index_table[32];

    memset(
        index_table,
        0,
        sizeof(index_table)
    );

    /*
     * Page 0 chứa ID 0..255.
     */
    if (
        !AS608_ReadIndexTable(
            0,
            index_table
        )
    )
    {
        GUI_Send("LIST_ERR\n");

        return;
    }

    /*
     * Buffer đủ để chứa:
     * LIST,1,2,3,...,127\n
     */
    char list_msg[512];

    memset(
        list_msg,
        0,
        sizeof(list_msg)
    );

    strcpy(
        list_msg,
        "LIST"
    );

    uint8_t found = 0;

    /*
     * Chỉ lấy ID 1..127.
     */
    for (uint16_t id = 1; id <= 127; id++)
    {
        uint16_t byte_index =
            id / 8;

        uint8_t bit_index =
            (id % 8);

        if (
            index_table[byte_index] &
            (1U << bit_index)
        )
        {
            char temp[8];

            sprintf(
                temp,
                ",%u",
                id
            );

            /*
             * Kiểm tra tránh tràn buffer.
             */
            if (
                strlen(list_msg) +
                strlen(temp) +
                2 <
                sizeof(list_msg)
            )
            {
                strcat(
                    list_msg,
                    temp
                );

                found = 1;
            }
        }
    }

    if (!found)
    {
        GUI_Send(
            "LIST,EMPTY\n"
        );
    }
    else
    {
        strcat(
            list_msg,
            "\n"
        );

        GUI_Send(list_msg);
    }
}


/* =========================================================
 * KHÔI PHỤC UART1 - AS608
 *
 * Dùng sau ADD/DEL hoặc khi UART1 bị lỗi/timeout
 * để lần giao tiếp tiếp theo bắt đầu từ trạng thái sạch.
 * ========================================================= */

void AS608_RecoverUART1(void)
{
    /*
     * Chỉ gọi khi main đang ở trạng thái blocking command,
     * không gọi từ ISR.
     */
    HAL_UART_Abort(&huart1);

    __HAL_UART_CLEAR_PEFLAG(&huart1);
    __HAL_UART_CLEAR_FEFLAG(&huart1);
    __HAL_UART_CLEAR_NEFLAG(&huart1);
    __HAL_UART_CLEAR_OREFLAG(&huart1);

    HAL_UART_DeInit(&huart1);
    MX_USART1_UART_Init();

    memset(
        RxBuffer,
        0,
        sizeof(RxBuffer)
    );

    HAL_Delay(100);
}


/* =========================================================
 * QUÉT TỰ ĐỘNG
 *
 * Không có ADD / DEL / TEST:
 *   GenImg -> GenChar1 -> Search
 *
 * Không gọi AS608_ClearRx() ở đầu MỖI vòng quét.
 * Việc đó có thể lấy mất packet của AS608 khi timing lệch.
 * Chỉ dọn UART1 khi đã biết có lỗi hoặc vừa kết thúc thao tác.
 * ========================================================= */

void AutoScan(void)
{
    memset(
        RxBuffer,
        0,
        sizeof(RxBuffer)
    );

    /*
     * GenImg
     */
    if (
        HAL_UART_Transmit(
            &huart1,
            AS608_GetImage,
            sizeof(AS608_GetImage),
            500
        ) != HAL_OK
    )
    {
        return;
    }

    /*
     * Chờ toàn bộ packet GenImg.
     * 1000 ms giúp tránh bỏ lỡ response.
     */
    if (
        HAL_UART_Receive(
            &huart1,
            RxBuffer,
            12,
            1000
        ) != HAL_OK
    )
    {
        return;
    }

    /*
     * Kiểm tra packet.
     */
    if (
        RxBuffer[0] != 0xEF ||
        RxBuffer[1] != 0x01 ||
        RxBuffer[6] != 0x07
    )
    {
        AS608_ClearRx();
        return;
    }

    /*
     * 0x02 = chưa có ngón.
     * Giữ nguyên màn hình "SAN SANG QUET".
     */
    if (RxBuffer[9] == 0x02)
    {
        return;
    }

    /*
     * Chỉ tiếp tục nếu chụp ảnh thành công.
     */
    if (RxBuffer[9] != 0x00)
    {
        AS608_ClearRx();
        return;
    }

    /*
     * Đã thấy ngón tay.
     */
    OLED_Show("DANG QUET");

    HAL_Delay(50);

    /*
     * Tạo đặc trưng Buffer 1.
     */
    if (!AS608_GenChar1_Function())
    {
        OLED_Show("LOI GENCHAR");
        AS608_RecoverUART1();
        HAL_Delay(500);
        OLED_Show("SAN SANG QUET");
        return;
    }

    /*
     * Search vùng 0..127.
     */
    uint16_t found_id = 0;

    if (
        AS608_SearchFingerprint(
            &found_id
        )
    )
    {
        char msg[32];

        sprintf(
            msg,
            "MATCH,%d\n",
            found_id
        );

        GUI_Send(msg);

        char oled[32];

        sprintf(
            oled,
            "ID: %d",
            found_id
        );

        OLED_Show(oled);
    }
    else
    {
        OLED_Show("SAI VAN TAY");

        GUI_Send("ERR_NOMATCH\n");
    }

    /*
     * Không quét lại khi ngón vẫn còn trên cảm biến.
     */
    HAL_Delay(300);

    OLED_Show("NHAC TAY RA");

    AS608_WaitFingerRemove();

    /*
     * Dọn UART1 sau cả chuỗi Search.
     */
    AS608_ClearRx();

    HAL_Delay(100);

    OLED_Show("SAN SANG QUET");
}


/* =========================================================
 * TEST TRUYỀN NHẬN 1000 KÝ TỰ
 *
 * Chỉ dùng cho nút Test1000 trên C#.
 * Không thay đổi cơ chế command cũ.
 *
 * C# gửi TEST1000\n
 * STM32 trả READY_TEST\n
 * C# gửi 1000 ký tự A
 * STM32 nhận đủ 1000 byte rồi echo lại.
 * ========================================================= */

void UART2_Test1000(void)
{
    uint8_t testBuffer[1000];

    /*
     * Dừng cơ chế nhận command từng byte hiện tại.
     */
    uart2_rx_active = 0;

    HAL_UART_AbortReceive(&huart2);

    /*
     * Xóa dữ liệu test cũ trong buffer UART2.
     */
    memset(
        testBuffer,
        0,
        sizeof(testBuffer)
    );

    /*
     * Báo cho C# biết STM32 đã sẵn sàng nhận 1000 byte.
     */
    GUI_Send("READY_TEST\n");

    /*
     * Nhận đúng 1000 byte.
     * Timeout 5 giây.
     */
    HAL_StatusTypeDef status =
        HAL_UART_Receive(
            &huart2,
            testBuffer,
            1000,
            5000
        );

    if (status == HAL_OK)
    {
        /*
         * Echo lại đúng 1000 byte nhận được.
         */
        HAL_UART_Transmit(
            &huart2,
            testBuffer,
            1000,
            5000
        );
    }
    else
    {
        /*
         * Không nhận đủ 1000 byte.
         */
        GUI_Send("TEST_TIMEOUT\n");
    }

    /*
     * Khởi động lại cơ chế nhận command cũ.
     */
    if (
        HAL_UART_Receive_IT(
            &huart2,
            &rx2_byte,
            1
        ) == HAL_OK
    )
    {
        uart2_rx_active = 1;
    }
    else
    {
        uart2_rx_active = 0;
        uart2_recover_pending = 1;
    }
}


/* =========================================================
 * XỬ LÝ LỆNH C#
 *
 * TEST
 * ADD,ID
 * DEL,ID
 * ========================================================= */

void ProcessCommand(void)
{
    char local_cmd[32];

    /*
     * Copy command trong vùng không bị ngắt
     * để tránh ISR sửa gui_cmd giữa lúc xử lý.
     */
    __disable_irq();

    if (!cmd_ready)
    {
        __enable_irq();
        return;
    }

    strncpy(
        local_cmd,
        gui_cmd,
        sizeof(local_cmd) - 1
    );

    local_cmd[sizeof(local_cmd) - 1] = '\0';

    cmd_ready = 0;
    cmd_idx = 0;

    memset(
        gui_cmd,
        0,
        sizeof(gui_cmd)
    );

    __enable_irq();


    /*
     * TEST
     */
    if (
        strcmp(
            local_cmd,
            "TEST"
        ) == 0
    )
    {
        OLED_Show("TEST OK");

        GUI_Send("SYS_OK\n");

        HAL_Delay(500);

        OLED_Show("SAN SANG QUET");

        return;
    }


    /*
     * TEST1000
     *
     * Test truyền nhận 1000 ký tự A giữa C# và STM32.
     */
    if (
        strcmp(
            local_cmd,
            "TEST1000"
        ) == 0
    )
    {
        OLED_Show("TEST 1000");

        UART2_Test1000();

        OLED_Show("SAN SANG QUET");

        return;
    }


    /*
     * LIST
     *
     * Đọc danh sách ID đang thực sự lưu trong AS608.
     */
    if (
        strcmp(
            local_cmd,
            "LIST"
        ) == 0
    )
    {
        AS608_ListFingerprints();

        OLED_Show("SAN SANG QUET");

        return;
    }


    /*
     * ADD,ID
     */
    if (
        strncmp(
            local_cmd,
            "ADD,",
            4
        ) == 0
    )
    {
        char *endptr;
        long id = strtol(
            local_cmd + 4,
            &endptr,
            10
        );

        /*
         * Phải nhập ID từ 0 đến 127
         * theo vùng Search hiện tại của bạn.
         */
        if (
            endptr == local_cmd + 4 ||
            *endptr != '\0' ||
            id < 0 ||
            id > 127
        )
        {
            OLED_Show("ID KHONG HOP LE");

            GUI_Send("ADD_ERR_ID\n");

            HAL_Delay(700);

            OLED_Show("SAN SANG QUET");

            return;
        }

        AddFingerprint(
            (uint16_t)id
        );

        OLED_Show("SAN SANG QUET");

        return;
    }


    /*
     * DEL,ID
     */
    if (
        strncmp(
            local_cmd,
            "DEL,",
            4
        ) == 0
    )
    {
        char *endptr;

        long id = strtol(
            local_cmd + 4,
            &endptr,
            10
        );

        if (
            endptr == local_cmd + 4 ||
            *endptr != '\0' ||
            id < 0 ||
            id > 127
        )
        {
            OLED_Show("ID KHONG HOP LE");

            GUI_Send("DEL_ERR_ID\n");

            HAL_Delay(700);

            OLED_Show("SAN SANG QUET");

            return;
        }

        if (
            AS608_Delete(
                (uint16_t)id
            )
        )
        {
            OLED_Show("XOA THANH CONG");
            GUI_Send("DEL_OK\n");
        }
        else
        {
            OLED_Show("LOI XOA");
            GUI_Send("DEL_ERR\n");
        }

        HAL_Delay(500);

        AS608_RecoverUART1();

        HAL_Delay(200);

        OLED_Show("SAN SANG QUET");

        return;
    }


    /*
     * Lệnh không xác định
     */
    GUI_Send("UNKNOWN_CMD\n");

    OLED_Show("SAN SANG QUET");
}


/* =========================================================
 * KHÔI PHỤC UART2
 *
 * Được gọi trong main, KHÔNG deinit/reinit trong ISR.
 * ========================================================= */

void UART2_Recover(void)
{
    uart2_recover_pending = 0;
    uart2_rx_active = 0;

    /*
     * Dừng nhận cũ.
     */
    HAL_UART_AbortReceive(&huart2);

    /*
     * Xóa trạng thái lỗi.
     */
    __HAL_UART_CLEAR_PEFLAG(&huart2);
    __HAL_UART_CLEAR_FEFLAG(&huart2);
    __HAL_UART_CLEAR_NEFLAG(&huart2);
    __HAL_UART_CLEAR_OREFLAG(&huart2);

    /*
     * Xóa buffer lệnh.
     */
    __disable_irq();

    memset(
        gui_cmd,
        0,
        sizeof(gui_cmd)
    );

    cmd_idx = 0;
    cmd_ready = 0;

    __enable_irq();

    /*
     * Khởi tạo lại USART2.
     */
    HAL_UART_DeInit(&huart2);

    MX_USART2_UART_Init();

    /*
     * Bắt đầu lại nhận 1 byte.
     */
    if (
        HAL_UART_Receive_IT(
            &huart2,
            &rx2_byte,
            1
        ) == HAL_OK
    )
    {
        uart2_rx_active = 1;
    }
    else
    {
        uart2_recover_pending = 1;
    }
}


/* =========================================================
 * MAIN
 * ========================================================= */

int main(void)
{
    HAL_Init();

    SystemClock_Config();

    MX_GPIO_Init();

    MX_I2C1_Init();

    MX_USART1_UART_Init();

    MX_USART2_UART_Init();


    /*
     * OLED
     */
    ssd1306_Init();

    AS608_RecoverUART1();

    OLED_Show("AS608 READY");

    HAL_Delay(1000);


    /*
     * UART2 nhận lệnh C#
     */
    if (
        HAL_UART_Receive_IT(
            &huart2,
            &rx2_byte,
            1
        ) == HAL_OK
    )
    {
        uart2_rx_active = 1;
    }
    else
    {
        uart2_recover_pending = 1;
    }

    GUI_Send("READY\n");

    AS608_ClearRx();

    OLED_Show("SAN SANG QUET");


    while (1)
    {
        /*
         * =================================================
         * PHỤC HỒI UART2 NẾU CÓ LỖI
         * =================================================
         */
        if (uart2_recover_pending)
        {
            UART2_Recover();
        }


        /*
         * Nếu vì bất kỳ lý do gì UART2 không còn
         * nhận interrupt thì tự bật lại.
         */
        if (
            uart2_rx_active == 0 &&
            uart2_recover_pending == 0
        )
        {
            if (
                HAL_UART_Receive_IT(
                    &huart2,
                    &rx2_byte,
                    1
                ) == HAL_OK
            )
            {
                uart2_rx_active = 1;
            }
            else
            {
                uart2_recover_pending = 1;
            }
        }


        /*
         * =================================================
         * XỬ LÝ LỆNH C#
         *
         * Có lệnh thì ưu tiên lệnh.
         * Không có lệnh thì quét tự động.
         * =================================================
         */
        if (cmd_ready)
        {
            ProcessCommand();

            HAL_Delay(20);

            continue;
        }


        /*
         * =================================================
         * QUÉT VÂN TAY TỰ ĐỘNG
         * =================================================
         */
        AutoScan();

        HAL_Delay(50);
    }
}


/* =========================================================
 * UART CALLBACK
 * ========================================================= */

void HAL_UART_RxCpltCallback(
    UART_HandleTypeDef *huart
)
{
    if (
        huart->Instance ==
        USART2
    )
    {
        uart2_rx_active = 0;

        /*
         * Kết thúc command bằng \n
         */
        if (rx2_byte == '\n')
        {
            if (!cmd_ready)
            {
                gui_cmd[cmd_idx] = '\0';

                cmd_ready = 1;

                cmd_idx = 0;
            }
        }
        else
        {
            /*
             * Bỏ \r
             */
            if (rx2_byte != '\r')
            {
                /*
                 * Chỉ nhận thêm dữ liệu nếu
                 * lệnh cũ đã được xử lý.
                 */
                if (!cmd_ready)
                {
                    if (
                        cmd_idx <
                        sizeof(gui_cmd) - 1
                    )
                    {
                        gui_cmd[cmd_idx++] =
                            (char)rx2_byte;
                    }
                    else
                    {
                        /*
                         * Tràn buffer -> bỏ command hiện tại
                         */
                        cmd_idx = 0;

                        memset(
                            gui_cmd,
                            0,
                            sizeof(gui_cmd)
                        );
                    }
                }
            }
        }

        /*
         * Tiếp tục nhận byte tiếp theo.
         */
        if (
            HAL_UART_Receive_IT(
                &huart2,
                &rx2_byte,
                1
            ) == HAL_OK
        )
        {
            uart2_rx_active = 1;
        }
        else
        {
            uart2_recover_pending = 1;
        }
    }
}


/* =========================================================
 * UART ERROR CALLBACK
 * ========================================================= */

void HAL_UART_ErrorCallback(
    UART_HandleTypeDef *huart
)
{
    if (
        huart->Instance ==
        USART2
    )
    {
        /*
         * Không DeInit UART ngay trong ISR.
         * Chỉ đánh dấu để main xử lý.
         */
        uart2_rx_active = 0;
        uart2_recover_pending = 1;

        /*
         * Xóa cờ lỗi UART2.
         */
        __HAL_UART_CLEAR_PEFLAG(&huart2);
        __HAL_UART_CLEAR_FEFLAG(&huart2);
        __HAL_UART_CLEAR_NEFLAG(&huart2);
        __HAL_UART_CLEAR_OREFLAG(&huart2);

        /*
         * Xóa command đang nhận dở.
         */
        cmd_idx = 0;

        memset(
            gui_cmd,
            0,
            sizeof(gui_cmd)
        );

        cmd_ready = 0;
    }
}


/* =========================================================
 * CLOCK
 * ========================================================= */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};

    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};


    RCC_OscInitStruct.OscillatorType =
        RCC_OSCILLATORTYPE_HSE;

    RCC_OscInitStruct.HSEState =
        RCC_HSE_ON;

    RCC_OscInitStruct.HSEPredivValue =
        RCC_HSE_PREDIV_DIV1;

    RCC_OscInitStruct.HSIState =
        RCC_HSI_ON;

    RCC_OscInitStruct.PLL.PLLState =
        RCC_PLL_ON;

    RCC_OscInitStruct.PLL.PLLSource =
        RCC_PLLSOURCE_HSE;

    RCC_OscInitStruct.PLL.PLLMUL =
        RCC_PLL_MUL9;


    if (
        HAL_RCC_OscConfig(
            &RCC_OscInitStruct
        ) != HAL_OK
    )
    {
        Error_Handler();
    }


    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK2;


    RCC_ClkInitStruct.SYSCLKSource =
        RCC_SYSCLKSOURCE_PLLCLK;

    RCC_ClkInitStruct.AHBCLKDivider =
        RCC_SYSCLK_DIV1;

    RCC_ClkInitStruct.APB1CLKDivider =
        RCC_HCLK_DIV2;

    RCC_ClkInitStruct.APB2CLKDivider =
        RCC_HCLK_DIV1;


    if (
        HAL_RCC_ClockConfig(
            &RCC_ClkInitStruct,
            FLASH_LATENCY_2
        ) != HAL_OK
    )
    {
        Error_Handler();
    }
}


/* =========================================================
 * I2C1
 * ========================================================= */

static void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;

    hi2c1.Init.ClockSpeed =
        400000;

    hi2c1.Init.DutyCycle =
        I2C_DUTYCYCLE_2;

    hi2c1.Init.OwnAddress1 =
        0;

    hi2c1.Init.AddressingMode =
        I2C_ADDRESSINGMODE_7BIT;

    hi2c1.Init.DualAddressMode =
        I2C_DUALADDRESS_DISABLE;

    hi2c1.Init.OwnAddress2 =
        0;

    hi2c1.Init.GeneralCallMode =
        I2C_GENERALCALL_DISABLE;

    hi2c1.Init.NoStretchMode =
        I2C_NOSTRETCH_DISABLE;


    if (
        HAL_I2C_Init(
            &hi2c1
        ) != HAL_OK
    )
    {
        Error_Handler();
    }
}


/* =========================================================
 * USART1 - AS608
 * ========================================================= */

static void MX_USART1_UART_Init(void)
{
    huart1.Instance =
        USART1;

    huart1.Init.BaudRate =
        57600;

    huart1.Init.WordLength =
        UART_WORDLENGTH_8B;

    huart1.Init.StopBits =
        UART_STOPBITS_1;

    huart1.Init.Parity =
        UART_PARITY_NONE;

    huart1.Init.Mode =
        UART_MODE_TX_RX;

    huart1.Init.HwFlowCtl =
        UART_HWCONTROL_NONE;

    huart1.Init.OverSampling =
        UART_OVERSAMPLING_16;


    if (
        HAL_UART_Init(
            &huart1
        ) != HAL_OK
    )
    {
        Error_Handler();
    }
}


/* =========================================================
 * USART2 - C#
 * ========================================================= */

static void MX_USART2_UART_Init(void)
{
    huart2.Instance =
        USART2;

    huart2.Init.BaudRate =
        9600;

    huart2.Init.WordLength =
        UART_WORDLENGTH_8B;

    huart2.Init.StopBits =
        UART_STOPBITS_1;

    huart2.Init.Parity =
        UART_PARITY_NONE;

    huart2.Init.Mode =
        UART_MODE_TX_RX;

    huart2.Init.HwFlowCtl =
        UART_HWCONTROL_NONE;

    huart2.Init.OverSampling =
        UART_OVERSAMPLING_16;


    if (
        HAL_UART_Init(
            &huart2
        ) != HAL_OK
    )
    {
        Error_Handler();
    }
}


/* =========================================================
 * GPIO
 * ========================================================= */

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    (void)GPIO_InitStruct;

    __HAL_RCC_GPIOA_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();

    __HAL_RCC_GPIOD_CLK_ENABLE();

    /*
     * Nếu project CubeMX của bạn có thêm GPIO khác
     * thì giữ nguyên phần cấu hình GPIO đó.
     */
}


/* =========================================================
 * ERROR
 * ========================================================= */

void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
    }
}
