#include <Adafruit_LiquidCrystal.h>

// Initialise the LCD
Adafruit_LiquidCrystal lcd(0);

// Morse characters
#define WHITESPACE '/'
#define LETTER_SPACE ' '
#define DOT '.'
#define DASH '-'
#define ILLEGAL '#'


// Time unit (1 dit)
#define T 200

// Buzzer tone frequency (hz)
#define BUZZER_FREQUENCY 700

// Static array size - dynamic arrays are bad for embedded development
#define ARRAY_SIZE 200

// Initiate state screen
#define GREETING_MORSE "-- --- .-. ... ."
#define GREETING_TEXT "MORSE"

// LCD dimensions
#define LCD_ROWS 2
#define LCD_COLS 16


volatile unsigned int timerCount = 0;
volatile unsigned int msCount = 0;
unsigned int buzzerCount = 0;


// 16,000,000 / 1000 = 16,000 ticks per millisecond
// 16,000 / 256 =  62.5 overflows per millisecond
unsigned int one_ms = round(F_CPU / 1000 / 256);

// 16,000,000 / 600 = 26,667 ticks per buzzer cycle
// 26,667 / 2 = 13,333 ticks per buzzer half cycle (on or off)
// 13,333 / 256 = 52.082 timer overflows per buzzer half cycle
unsigned int buzzer_half_period = round(F_CPU / BUZZER_FREQUENCY / 2 / 256);

char morseSequence[ARRAY_SIZE] = {}; // Array of morse code symbols
char decodedText[ARRAY_SIZE] = {}; // Converted morse code sequence as a string

volatile unsigned int i = 0;

// Global flags to handle CPU heavy operations outside the ISRs
volatile bool display = false;
volatile bool fin = false;
volatile bool buzzer = false;

// Binary trie of Morse code: each node is one more dot/dash symbol deep,
// and holds the decoded character once a full code has been matched.
class MorseTree {
public:
    MorseTree() {
        root = new Node('\0');
        insertCode(".-", 'A');
        insertCode("-...", 'B');
        insertCode("-.-.", 'C');
        insertCode("-..", 'D');
        insertCode(".", 'E');
        insertCode("..-.", 'F');
        insertCode("--.", 'G');
        insertCode("....", 'H');
        insertCode("..", 'I');
        insertCode(".---", 'J');
        insertCode("-.-", 'K');
        insertCode(".-..", 'L');
        insertCode("--", 'M');
        insertCode("-.", 'N');
        insertCode("---", 'O');
        insertCode(".--.", 'P');
        insertCode("--.-", 'Q');
        insertCode(".-.", 'R');
        insertCode("...", 'S');
        insertCode("-", 'T');
        insertCode("..-", 'U');
        insertCode("...-", 'V');
        insertCode(".--", 'W');
        insertCode("-..-", 'X');
        insertCode("-.--", 'Y');
        insertCode("--..", 'Z');
        insertCode("-----", '0');
        insertCode(".----", '1');
        insertCode("..---", '2');
        insertCode("...--", '3');
        insertCode("....-", '4');
        insertCode(".....", '5');
        insertCode("-....", '6');
        insertCode("--...", '7');
        insertCode("---..", '8');
        insertCode("----.", '9');
        insertCode("..--..", '?');
        insertCode("-.-.--", '!');
        insertCode(".-.-.-", '.');
        insertCode("--..--", ',');
        insertCode("---...", ':');
        insertCode("-....-", '-');
        insertCode("-....-", '-');
        insertCode("-..-.", '/');
        insertCode("-...-", '=');
        insertCode(".----.", '\'');
        insertCode("-.--.", '(');
        insertCode("-.--.-", ')');
        insertCode(".-..-.", '"');
        insertCode(".--.-.", '@');
        insertCode("...-..-", '$');
        insertCode(".-...", '&');
        insertCode("-.-.-.", ';');
        insertCode("..--.-", '_');
    }

    // Decodes one Morse letter out of `sequence`, starting at `pos`.
    // Consumes DOT/DASH symbols until a SPACE or `length` is reached,
    // advancing `pos` past everything consumed.
    // Returns the decoded character, or '#' if the code isn't recognised.
    char decodeLetter(const char *sequence, const unsigned int length, unsigned int &pos) const {
        const Node *current = root;
        while (current && pos < length && sequence[pos] != WHITESPACE && sequence[pos] != LETTER_SPACE) {
            current = (sequence[pos] == DOT) ? current->dot : current->dash;
            if (sequence[pos] != WHITESPACE) pos++;
        }
        if (!current) while (pos < length && sequence[pos] != WHITESPACE && sequence[pos] != LETTER_SPACE) pos++;
        return current ? current->character : ILLEGAL;
    }

private:
    struct Node {
        char character;
        Node *dot;
        Node *dash;

        explicit Node(const char ch) : character(ch), dot(nullptr), dash(nullptr) {
        }
    };

    Node *root;

    // Insert a character into the tree based on its Morse code representation
    void insertCode(const char *code, const char ch) const {
        Node *current = root;
        for (const char *p = code; *p; p++) {
            if (*p == DOT) {
                if (!current->dot)
                    current->dot = new Node('\0');
                current = current->dot;
            } else if (*p == DASH) {
                if (!current->dash)
                    current->dash = new Node('\0');
                current = current->dash;
            }
        }
        current->character = ch;
    }
};

MorseTree morseTree;

void setup() {
    cli(); // Disable global interrupts

    // Set pin 8 and pin 13 as output
    DDRB |= (1 << DDB0) | (1 << DDB5);
    PORTB &= ~((1 << PORTB0) | (1 << PORTB5));

    // Set pin 2 as input
    DDRD &= ~(1 << DDD2);
    PORTD |= (1 << PORTD2);

    // Enable INT0 on any logical change
    EICRA &= ~(1 << ISC01);
    EICRA |= (1 << ISC00);
    EIMSK |= (1 << INT0);

    // Setup timer2

    // Clear timer control registers
    TCCR2A = 0;
    TCCR2B = 0;

    TCNT2 = 0; // Initialise counter value to 0

    // No prescaler
    TCCR2B &= ~((1 << CS22) | (1 << CS21));
    TCCR2B |= (1 << CS20);

    TIMSK2 |= (1 << TOIE2); // Enable timer overflow interrupt


    sei(); // Enable global interrupts

    // 1. Boot up the screen
    lcd.begin(LCD_COLS, LCD_ROWS);
    lcd.setBacklight(1);
    lcd.clear();
    lcd.print(GREETING_MORSE);
    lcd.setCursor(0, 1);
    lcd.print(GREETING_TEXT);
}


// Reset the timer and millisecond counters
void resetTimer() {
    msCount = 0;
    timerCount = 0;
    TCNT2 = 0;
}

char discriminateSignal(const unsigned int duration) {
    if (duration < (2 * T)) return DOT;
    if (duration < (5 * T)) return DASH;
    return '\0'; // Invalid signal or stop
}

// Decode morse string
void morseDecode(const char *sequence, const unsigned int length) {
    int k = 0;
    unsigned int j = 0;
    while (j < length - 1) {
        if (sequence[j] == WHITESPACE) {
            decodedText[k++] = ' ';
            j++;
            continue;
        }
        if (sequence[j] == LETTER_SPACE) {
            j++;
            continue;
        }

        if (const char decoded = morseTree.decodeLetter(sequence, length, j); decoded != '\0') {
            decodedText[k++] = decoded;
        }

        // decodeLetter stops right on the separator that ended the letter;
        // consume it explicitly here rather than relying on a loop increment.
        if (j < length && sequence[j] == WHITESPACE) {
            decodedText[k++] = ' ';
            j++;
        } else if (j < length && sequence[j] == LETTER_SPACE) {
            j++;
        }
    }
    decodedText[k] = '\0'; // Null-terminate the string
}

// Take an aray of text(chars)and display them on the LCD
void LCDPrintWrap(const char *str) {
    lcd.clear();

    int column = 0;
    int row = 0;

    for (unsigned int j = 0; j < strlen(str); j++) {
        // Stop if we hit an empty/null character
        if (str[j] == '\0') break;

        lcd.print(str[j]);
        column++;

        // Wrap to the next line if the current line is full
        if (column == LCD_COLS && row < LCD_ROWS - 1) {
            row++;
            column = 0;
            lcd.setCursor(column, row);
        }
        // Stop printing if the entire screen (32 chars) is full
        else if (column == LCD_COLS && row == LCD_ROWS - 1) break;
    }
}

// Print both morse and translation
void LCDDoublePrint(const char *str1, const char *str2) {
    lcd.clear();

    if (const unsigned int len1 = strlen(str1); len1 > LCD_COLS)
        str1 += len1 - (LCD_COLS * sizeof(char)); // Tail str1
    lcd.print(str1);

    lcd.setCursor(0, 1);
    if (const unsigned int len2 = strlen(str2); len2 > LCD_COLS)
        str2 += len2 - (LCD_COLS * sizeof(char)); // Tail str2
    lcd.print(str2);
}


// Button interrupt
ISR(INT0_vect) {
    if (i >= ARRAY_SIZE) fin = true;
    else {
        // Rising Edge
        if (PIND & (1 << PIND2)) {
            PORTB |= (1 << PORTB5); // Activate LED and buzzer
            buzzer = true;
            if (i > 0) {
                // Ignore first pause
                if (msCount >= (2.5 * T) && msCount < (7 * T) && morseSequence[i - 1] != LETTER_SPACE)
                    morseSequence[i++] = LETTER_SPACE;
                if (msCount >= (7 * T) && msCount < (21 * T))
                    morseSequence[i++] = WHITESPACE;
                if (msCount >= (21 * T)) {
                    morseSequence[i] = '\0'; // Stop signal
                    fin = true;
                }
            }
        }

        // Falling Edge
        else {
            PORTB &= ~(1 << PORTB5); // Deactivate LED and buzzer
            buzzer = false;

            morseSequence[i] = discriminateSignal(msCount);
            if (morseSequence[i] != '\0') {
                display = true;
                i++;
            } else fin = true;
        }
        resetTimer();
    }
}

// Timer overflow interrupt service routine
ISR(TIMER2_OVF_vect) {
    timerCount++;
    if (timerCount >= one_ms) // Approximately 1 ms has passed
    {
        timerCount = 0;
        msCount++;
    }

    // Enable buzzer if buzzer flag is on
    // Note: the `tone()` function utilises interrupt 2, so we couldn't use it.
    if (buzzer) {
        buzzerCount++;
        if (buzzerCount >= buzzer_half_period) {
            buzzerCount = 0;
            PORTB ^= (1 << PORTB0); // Toggle buzzer pin
        }
    } else {
        PORTB &= ~(1 << PORTB0); // Ensure buzzer pin is low
        buzzerCount = 0;
    }

    // After ~4.2s of inactivity, wrap it up
    if (msCount >= (21 * T) && morseSequence[0] != '\0') fin = true;
}

void loop() {
    // Display result and reset
    if (fin) {
        morseDecode(morseSequence, i);
        // Reset morse sequence
        while (i > 0) morseSequence[--i] = '\0';
        resetTimer();
        display = false;
        buzzer = false;
        LCDPrintWrap(decodedText);
        fin = false;
    }
    if (display) {
        morseDecode(morseSequence, i);
        LCDDoublePrint(morseSequence, decodedText);
        display = false;
    }
}
