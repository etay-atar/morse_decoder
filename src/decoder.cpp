#include <Arduino.h>
#include <Adafruit_LiquidCrystal.h>

// Initialize the LCD
Adafruit_LiquidCrystal lcd(0);

#define SPACE ' '
#define DOT '.'
#define DASH '-'

#define SHORT_DURATION 200

#define ARRAY_SIZE 100

unsigned int timerCount = 0;
unsigned int msCount = 0;

unsigned int rawSequence[ARRAY_SIZE * 2]; // Array of pulse and space durations
char convertedSequence[ARRAY_SIZE];       // Array of morse code symbols
char morseSequence[ARRAY_SIZE];           // Converted morse code sequence as a string

unsigned int i = 0;

const int shortDuration = SHORT_DURATION;    // Duration threshold for short pulse
const int longDuration = 3 * shortDuration;  // Duration threshold for long pulse
const int spaceDuration = 7 * shortDuration; // Duration threshold for space
const int stopDuration = 3 * spaceDuration;  // Duration threshold for stop

// Binary trie of Morse code: each node is one more dot/dash symbol deep,
// and holds the decoded character once a full code has been matched.
class MorseTree
{
public:
    MorseTree()
    {
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
    }

    // Decodes one Morse letter out of `sequence`, starting at `pos`.
    // Consumes DOT/DASH symbols until a SPACE or `length` is reached,
    // advancing `pos` past everything consumed.
    // Returns the decoded character, or '\0' if the code isn't recognized.
    char decodeLetter(const char *sequence, const unsigned int length, unsigned int &pos) const
    {
        Node *current = root;
        while (current && pos < length && sequence[pos] != SPACE)
        {
            current = (sequence[pos] == DOT) ? current->dot : current->dash;
            pos++;
        }
        return current ? current->character : '\0';
    }

private:
    struct Node
    {
        char character;
        Node *dot;
        Node *dash;

        explicit Node(const char ch) : character(ch), dot(nullptr), dash(nullptr) {}
    };

    Node *root;

    // Insert a character into the tree based on its Morse code representation
    void insertCode(const char *code, const char ch) const {
        Node *current = root;
        for (const char *p = code; *p; p++)
        {
            if (*p == DOT)
            {
                if (!current->dot)
                    current->dot = new Node('\0');
                current = current->dot;
            }
            else if (*p == DASH)
            {
                if (!current->dash)
                    current->dash = new Node('\0');
                current = current->dash;
            }
        }
        current->character = ch;
    }
};

MorseTree morseTree;

void setup()
{
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

    TCNT2 = 0; // Initialize counter value to 0

    TCCR2B &= ~((1 << CS22) | (1 << CS21) | (1 << CS20)); // Clear prescaler bits

    TIMSK2 |= (1 << TOIE2); // Enable timer overflow interrupt

    // 16,000,000 / 8 = 2,000,000 ticks per second
    // 2,000,000 / 1000 = 2,000 ticks per millisecond
    // 2,000 ticks per millisecond / 256 (max timer count) = 7.8125 overflows per millisecond
    // So we can count 8 overflows to approximate 1 millisecond

    sei(); // Enable global interrupts

    // 1. Boot up the screen
    lcd.begin(16, 2);
    lcd.setBacklight(1);
    lcd.clear();
  	lcd.print("Ready to print...");
    // 2. Display decoded Morse text buffer when available
    void displayArrayOnLCD(char *textArray, int length);
}



void loop()
{
}

// Reset the timer and millisecond counters
void resetTimer()
{
    msCount = 0;
    timerCount = 0;
}

void convertArray(unsigned int *rawSequence, unsigned int length)
{
    // TODO: Calibrate threshholds
    for (unsigned int j = 0; j < length; j++)
    {
        if (j % 2 == 0) // Even index: pulse duration
        {
            if (rawSequence[j] < longDuration)
            {
                convertedSequence[j] = DOT;
            }
            else if (rawSequence[j] >= longDuration && rawSequence[j] < stopDuration)
            {
                convertedSequence[j] = DASH;
            }
            else if (rawSequence[j] >= stopDuration)
            {
                break;
            }
        }
        else // Odd index: space duration
        {
            if (rawSequence[j] <= spaceDuration)
            {
                convertedSequence[j] = SPACE;
            }
            else if (rawSequence[j] >= stopDuration)
            {
                break;
            }
        }
    }
}

void morseDecode(char *convertedSequence, unsigned int length)
{
    int k = 0;
    for (unsigned int j = 0; j < length; j++)
    {
        if (convertedSequence[j] == SPACE)
        {
            morseSequence[k] = ' ';
            k++;
        }
        else
        {
            char decoded = morseTree.decodeLetter(convertedSequence, length, j);
            if (decoded != '\0')
            {
                morseSequence[k] = decoded;
                k++;
            }
        }
    }
    morseSequence[k] = '\0'; // Null-terminate the string
}

// Take an aray of text(chars)and display them on the LCD
void displayArrayOnLCD(char *textArray, const int length = ARRAY_SIZE) {
    lcd.clear();
    lcd.setCursor(0, 0);

    int column = 0;
    int row = 0;

    for (int j = 0; j < length; j++) {
        // Stop if we hit an empty/null character
        if (textArray[j] == '\0') {
            break;
        }

        lcd.print(textArray[j]);
        column++;

        // Wrap to the second line if the first line is full
        if (column == 16 && row == 0) {
            row = 1;
            column = 0;
            lcd.setCursor(column, row);
        }
        // Stop printing if the entire screen (32 chars) is full
        else if (column == 16 && row == 1) {
            break;
        }
    }
}

ISR(INT0_vect)
{
    if (msCount < stopDuration)
    {
        // If the time between pulses is less than 4.2 seconds, store the count
        if (msCount)
        {
            rawSequence[i] = msCount;
            i++;
        }
    }
    else
    {
        // If the time between pulses is greater than or equal to 4.2 seconds, reset the sequence
        convertArray(rawSequence, i);
        morseDecode(convertedSequence, i);
        i = 0;

    }

    resetTimer();

    if (PIND & (1 << PIND2))
    {
        PORTB |= (1 << PORTB5);
        tone(8, 800);
    }
    else
    {
        PORTB &= ~(1 << PORTB5);
        noTone(8);
    }
}

ISR(TIMER2_OVF_vect)
{
    timerCount++;
    if (timerCount >= 62) // Approximately 1 ms has passed
    {
        timerCount = 0;
        msCount++;
    }
}