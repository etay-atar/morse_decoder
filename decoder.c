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

// Morse code tree node structure

typedef struct MorseNode
{
    char character;
    struct MorseNode *dot;  // Pointer to the next node for a dot
    struct MorseNode *dash; // Pointer to the next node for a dash
} MorseNode;
MorseNode* newNode(char ch);
void insertCode(MorseNode *root, const char *code, char ch);
MorseNode* buildMorseTree();

// Create a new MorseNode
MorseNode *newNode(char ch)
{
    MorseNode *node = (MorseNode *)malloc(sizeof(MorseNode));
    node->character = ch;
    node->dot = NULL;
    node->dash = NULL;
    return node;
}

// Insert a character into the Morse code tree based on its Morse code representation
void insertCode(MorseNode *root, const char *code, char ch)
{
    MorseNode *current = root;
    for (const char *p = code; *p; p++)
    {
        if (*p == '.')
        {
            if (!current->dot)
                current->dot = newNode('\0');
            current = current->dot;
        }
        else if (*p == '-')
        {
            if (!current->dash)
                current->dash = newNode('\0');
            current = current->dash;
        }
    }
    current->character = ch;
}

// Build the Morse code tree
MorseNode *buildMorseTree()
{
    MorseNode *root = newNode('\0');
    insertCode(root, ".-", 'A');
    insertCode(root, "-...", 'B');
    insertCode(root, "-.-.", 'C');
    insertCode(root, "-..", 'D');
    insertCode(root, ".", 'E');
    insertCode(root, "..-.", 'F');
    insertCode(root, "--.", 'G');
    insertCode(root, "....", 'H');
    insertCode(root, "..", 'I');
    insertCode(root, ".---", 'J');
    insertCode(root, "-.-", 'K');
    insertCode(root, ".-..", 'L');
    insertCode(root, "--", 'M');
    insertCode(root, "-.", 'N');
    insertCode(root, "---", 'O');
    insertCode(root, ".--.", 'P');
    insertCode(root, "--.-", 'Q');
    insertCode(root, ".-.", 'R');
    insertCode(root, "...", 'S');
    insertCode(root, "-", 'T');
    insertCode(root, "..-", 'U');
    insertCode(root, "...-", 'V');
    insertCode(root, ".--", 'W');
    insertCode(root, "-..-", 'X');
    insertCode(root, "-.--", 'Y');
    insertCode(root, "--..", 'Z');
    insertCode(root, "-----", '0');
    insertCode(root, ".----", '1');
    insertCode(root, "..---", '2');
    insertCode(root, "...--", '3');
    insertCode(root, "....-", '4');
    insertCode(root, ".....", '5');
    insertCode(root, "-....", '6');
    insertCode(root, "--...", '7');
    insertCode(root, "---..", '8');
    insertCode(root, "----.", '9');
    return root;
}

MorseNode *morseTreeRoot;

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

    morseTreeRoot = buildMorseTree();

    sei(); // Enable global interrupts

    // 1. Boot up the screen
    lcd.begin(16, 2);
    lcd.setBacklight(1);
    lcd.clear();
  	lcd.print("Ready to print...");
    // 2. Display decoded Morse text buffer when available
    displayArrayOnLCD(morseSequence, ARRAY_SIZE);
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
            MorseNode *current = morseTreeRoot;
            while (j < length && convertedSequence[j] != SPACE)
            {
                if (convertedSequence[j] == DOT)
                {
                    current = current->dot;
                }
                else if (convertedSequence[j] == DASH)
                {
                    current = current->dash;
                }
                j++;
            }
            if (current && current->character != '\0')
            {
                morseSequence[k] = current->character;
                k++;
            }
        }
    }
    morseSequence[k] = '\0'; // Null-terminate the string
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

// Take an aray of text(chars)and display them on the LCD
void displayArrayOnLCD(char *textArray, int length) {
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