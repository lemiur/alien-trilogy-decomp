typedef void (*MenuCallback)();
typedef struct MenuEntry {
    unsigned short flags;
    short y;
    short value;
    short limit;
    const char *label;
    const char *const *choices;
    MenuCallback callback0, callback1, callback2;
} MenuEntry;
extern MenuEntry g_password_prompt_menu[];
extern char g_password_key_characters[36];
extern unsigned char g_request_kind2_color[4];
extern unsigned char g_request_default_color[4];
extern char *strcpy(char *destination, const char *source);
extern short f_1c308(const char *text);
extern void f_1c1b0(const char *text, int x, short y, const unsigned char *color);
extern void f_18724(int unused, int y);

/* Draws the password keyboard: 36 keys in rows of 11, the last three
 * being Delete/Accept/Quit, with the cursor key highlighted and
 * bracketed. */
void f_18820(void)
{
    char text[16];
    short cursor = g_password_prompt_menu[0].value;
    short selected_width = 0;
    short key = 0;
    short x;
    short y = 50;
    short row;
    short column;
    short width;
    const unsigned char *color;

    for (row = 0; row < 4; row++, y += 24) {
        x = 30;
        for (column = 0; column < 11; column++, key++) {
            if ((unsigned short)key >= 36)
                break;
            switch (key) {
            case 33:
                strcpy(text, "Delete ");
                break;
            case 34:
                strcpy(text, "Accept ");
                break;
            case 35:
                strcpy(text, "Quit ");
                break;
            default:
                text[0] = g_password_key_characters[key];
                text[1] = 0;
                break;
            }
            width = key >= 33 ? f_1c308(text) - 12 : 0;
            if (cursor == key) {
                color = g_request_kind2_color;
                selected_width = width;
            } else {
                color = g_request_default_color;
            }
            f_1c1b0(text, x, y, color);
            x += key >= 33 ? 96 : 24;
        }
    }
    y = g_password_prompt_menu[0].value / 11;
    column = g_password_prompt_menu[0].value % 11;
    if (cursor >= 33)
        x = column * 96 + 30;
    else
        x = column * 24 + 30;
    y = y * 24 + 50;
    color = g_request_kind2_color;
    f_1c1b0("[", x - 12, y, color);
    f_1c1b0("]", x + selected_width + 12, y, color);
    f_18724(0, 180);
}
