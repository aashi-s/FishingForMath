// Works best in Visual Studio Code if you set:
//   Settings -> Features -> Terminal -> Local Echo Latency Threshold = -1

#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono> // for dealing with time intervals
#include <cmath> // for max() and min()
#include <termios.h> // to control terminal modes
#include <unistd.h> // for usleep()
#include <fcntl.h> // to enable / disable non-blocking read()

// Because we are only using #includes from the standard, names shouldn't conflict
using namespace std;

// Disable JUST this warning (in case students choose not to use some of these constants)     
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"

struct position { int row; int col; };
typedef vector< string > stringvector;
position currentPosition {1,1};

// Constants
const char NULL_CHAR     { 'z' };
const char CREATE_CHAR   { 'c' };

const string ANSI_START { "\033[" };
const string START_COLOUR_PREFIX {"1;"};
const string START_COLOUR_SUFFIX {"m"};
const string STOP_COLOUR  {"\033[0m"};

const unsigned int COLOUR_IGNORE  { 0 }; // this is a little dangerous but should work out OK 
const unsigned int COLOUR_BLACK   { 30 };
const unsigned int COLOUR_RED     { 31 };
const unsigned int COLOUR_GREEN   { 32 };
const unsigned int COLOUR_YELLOW  { 33 };
const unsigned int COLOUR_BLUE    { 34 };
const unsigned int COLOUR_MAGENTA { 35 };
const unsigned int COLOUR_CYAN    { 36 };
const unsigned int COLOUR_WHITE   { 37 };

bool alive = true;
bool won = false;

#pragma clang diagnostic pop

const stringvector FISHERMAN_SPRITE
{
{"    ____"},
{"   /    \\"},
{"__/______\\__"},
{"  \\ '  ' /      //\\"},
{"   \\_--_/      //  \\"},
{"   _|  |_     //    \\"},
{"  /      \\   //      \\"},
{" | |    | | //        \\"},
{"  O ____ O //          q"},
{"   |    |"},
{"   | |  |"},
{"   |_|__|"},
{"    O  O "}
};

const stringvector YOU_WIN
{
{"  ___   ___    ___     ___   ___        "},
{"  \\  \\/   /   /   \\    |  | |  |       "},
{"   \\     /   /  O  \\   |  |_|  |       "},
{"    |   |    \\     /   |       |       "},
{"    |___|     \\___/    \\_______/       "},
{"                                        "},
{"   ___  ___  ___   ___  ___   ___       "},
{"   \\  \\/   \\/   /  |  | |  \\ |  |   "},
{"    \\    /\\    /   |  | |   \\|  |    "},
{"     \\  /  \\  /    |  | |  |\\   |    "},
{"      \\/    \\/     |__| |__| \\__|    "},
{"                                        "}
};

const stringvector YOU_LOST
{
{"                                  "},
{"  ___   ___    ___     ___   ___        "},
{"  \\  \\/   /   /   \\    |  | |  |       "},
{"   \\     /   /  O  \\   |  |_|  |       "},
{"    |   |    \\     /   |       |       "},
{"    |___|     \\___/    \\_______/       "},
{"                                        "},
{"                                  "},
{"  _      ___     ____  _______   "},
{" | |    /   \\   |  __| |__ __|   "},
{" | |   /  O  \\  |__|__   | |     "},
{" | |__ \\     /  ___| |   | |       "},
{" |____| \\___/   |____/   |_|    "},
{"                                  "},
{"                                  "}
};

struct fishie
{
    position position {1,1};
    bool swimming = true;
    unsigned int colour = COLOUR_BLUE;
    float speed = 1.0;
};

typedef vector< fishie > fishvector;

// Globals

struct termios initialTerm;
default_random_engine generator;
uniform_int_distribution<int> movement(-1,1);
uniform_int_distribution<unsigned int> fishcolour( COLOUR_RED, COLOUR_WHITE );

// Utilty Functions

// These two functions are taken from StackExchange and are
// all of the "magic" in this code.
auto SetupScreenAndInput() -> void
{
    struct termios newTerm;
    // Load the current terminal attributes for STDIN and store them in a global
    tcgetattr(fileno(stdin), &initialTerm);
    newTerm = initialTerm;
    // Mask out terminal echo and enable "noncanonical mode"
    // " ... input is available immediately (without the user having to type
    // a line-delimiter character), no input processing is performed ..."
    newTerm.c_lflag &= ~ICANON;
    newTerm.c_lflag &= ~ECHO;
    newTerm.c_cc[VMIN] = 1;

    // Set the terminal attributes for STDIN immediately
    auto result { tcsetattr(fileno(stdin), TCSANOW, &newTerm) };
    if ( result < 0 ) { cerr << "Error setting terminal attributes [" << result << "]" << endl; }
}
auto TeardownScreenAndInput() -> void
{
    // Reset STDIO to its original settings
    tcsetattr( fileno( stdin ), TCSANOW, &initialTerm );
}
auto SetNonblockingReadState( bool desiredState = true ) -> void
{
    auto currentFlags { fcntl( 0, F_GETFL ) };
    if ( desiredState ) { fcntl( 0, F_SETFL, ( currentFlags | O_NONBLOCK ) ); }
    else { fcntl( 0, F_SETFL, ( currentFlags & ( ~O_NONBLOCK ) ) ); }
}
// Everything from here on is based on ANSI codes
// Note the use of "flush" after every write to ensure the screen updates
auto ClearScreen() -> void { cout << ANSI_START << "2J" << flush; }
auto MoveTo( unsigned int x, unsigned int y ) -> void { cout << ANSI_START << x << ";" << y << "H" << flush; }
auto HideCursor() -> void { cout << ANSI_START << "?25l" << flush; }
auto ShowCursor() -> void { cout << ANSI_START << "?25h" << flush; }
auto GetTerminalSize() -> position
{
    // This feels sketchy but is actually about the only way to make this work
    MoveTo(999,999);
    cout << ANSI_START << "6n" << flush ;
    string responseString;
    char currentChar { static_cast<char>( getchar() ) };
    while ( currentChar != 'R')
    {
        responseString += currentChar;
        currentChar = getchar();
    }
    // format is ESC[nnn;mmm ... so remove the first 2 characters + split on ; + convert to unsigned int
    // cerr << responseString << endl;
    responseString.erase(0,2);
    // cerr << responseString << endl;
    auto semicolonLocation = responseString.find(";");
    // cerr << "[" << semicolonLocation << "]" << endl;
    auto rowsString { responseString.substr( 0, semicolonLocation ) };
    auto colsString { responseString.substr( ( semicolonLocation + 1 ), responseString.size() ) };
    // cerr << "[" << rowsString << "][" << colsString << "]" << endl;
    auto rows = stoul( rowsString );
    auto cols = stoul( colsString );
    position returnSize { static_cast<int>(rows), static_cast<int>(cols) };
    // cerr << "[" << returnSize.row << "," << returnSize.col << "]" << endl;
    return returnSize;
}
auto MakeColour( string inputString,
                 const unsigned int foregroundColour = COLOUR_WHITE,
                 const unsigned int backgroundColour = COLOUR_IGNORE ) -> string
{
    string outputString;
    outputString += ANSI_START;
    outputString += START_COLOUR_PREFIX;
    outputString += to_string( foregroundColour );
    if ( backgroundColour )
    {
        outputString += ";";
        outputString += to_string( ( backgroundColour + 10 ) ); // Tacky but works
    }
    outputString += START_COLOUR_SUFFIX;
    outputString += inputString;
    outputString += STOP_COLOUR;
    return outputString;
}

auto DrawSprite( position targetPosition, stringvector sprite)
{
    MoveTo( targetPosition.row, targetPosition.col );
    for ( auto currentSpriteRow = 0 ; currentSpriteRow < static_cast<int>(sprite.size()) ; currentSpriteRow++ )
    {
        cout << MakeColour( sprite[currentSpriteRow] );
        //stringvectors aren't going to move, can comment out
        MoveTo( ( targetPosition.row + ( currentSpriteRow + 1 ) ) , targetPosition.col );     
    };
}

// Fish Logic
auto UpdateFishPositions( fishvector & fishies) -> void
{
    // Update the position of each fish
    // Use a reference so that the actual position updates

    // When user wins, pushes all fish rightwards
    if(won)
    {
        for ( auto & currentFish  : fishies )
        {
            auto proposedRow { currentFish.position.row };
            auto proposedCol { currentFish.position.col - 1 };
            currentFish.position.row = max(  1, min( 20, proposedRow ) );
            currentFish.position.col = max(  1, min( 40, proposedCol ) );
        }
    }

    // When user loses, pushes all fish downwards
    if (alive == false)
    {
        for ( auto & currentFish  : fishies )
        {
            auto proposedRow { currentFish.position.row + 1 };
            auto proposedCol { currentFish.position.col };
            currentFish.position.row = max(  1, min( 20, proposedRow ) );
            currentFish.position.col = max(  1, min( 40, proposedCol ) );
        }
    }
}

auto CreateFishie( fishvector & fishies ) -> void
{
    // Create random coordinates for fish spawnpoint
    uniform_int_distribution<int> spawnRow(10,15);
    uniform_int_distribution<int> spawnColumn(25,40);

    fishie newFish {
        .position = { .row = spawnRow(generator), .col = spawnColumn(generator) } ,
        .swimming = true ,
        .colour = fishcolour(generator),
        .speed = 1.0
    };
    fishies.push_back( newFish );
}

auto DrawFishies( fishvector fishies ) -> void
{
    for ( auto currentFish  : fishies )
    {
        MoveTo( currentFish.position.row + 5, currentFish.position.col );
        cout << MakeColour( "><((('>", currentFish.colour ) << flush;
    }
    MoveTo(0,0);
    cout << "Welcome to Fishing For Math. When you answer a question right, you give a fish to the fisherman!" << endl;
    cout << "When you get a question wrong, all the fish die. Answer 10 questions right to win!" << endl;
    DrawSprite ({currentPosition.row + 3, (currentPosition.col + 2)}, FISHERMAN_SPRITE);      
}

auto main() -> int
{
    // Set Up the system to receive input
    SetupScreenAndInput();

    // Check that the terminal size is large enough for our fishies
    const position TERMINAL_SIZE { GetTerminalSize() };
    if ( ( TERMINAL_SIZE.row < 30 ) or ( TERMINAL_SIZE.col < 50 ) )
    {
        ShowCursor();
        TeardownScreenAndInput();
        cout << endl <<  "Terminal window must be at least 30 by 50 to run this game" << endl;
        return EXIT_FAILURE;
    }

    // State Variables
    fishvector fishies;

    char currentChar { CREATE_CHAR }; // the first act will be to create a fish
    string currentCommand;

    bool allowBackgroundProcessing { false };
    bool showCommandline { true };

    auto startTimestamp { chrono::steady_clock::now() };
    auto endTimestamp { startTimestamp };
    int elapsedTimePerTick { 100 }; // Every 0.1s check on things

    ClearScreen();
    SetNonblockingReadState( allowBackgroundProcessing );

    CreateFishie(fishies);

    // Initialize game variables
    string answer = "";
    int score = 0;

    while( true )
    {
        endTimestamp = chrono::steady_clock::now();
        auto elapsed { chrono::duration_cast<chrono::milliseconds>( endTimestamp - startTimestamp ).count() };
        // We want to process input and update the world when EITHER
        // (a) there is background processing and enough time has elapsed
        // (b) when we are not allowing background processing.
        if ( 
                 ( allowBackgroundProcessing and ( elapsed >= elapsedTimePerTick ) )
              or ( not allowBackgroundProcessing )
           )
        {
            UpdateFishPositions( fishies );
            ClearScreen();
            DrawFishies( fishies );

            MoveTo( 5, 75);

            uniform_int_distribution<int> firstValueRandomizer(1,12);
            uniform_int_distribution<int> secondValueRandomizer(1,12);
            uniform_int_distribution<int> operationRandomizer(1,3);

            int firstValue = firstValueRandomizer(generator);
            int secondValue = secondValueRandomizer(generator);
            int operation = operationRandomizer(generator);

            // Displays question based on randomized operation (addition, subtraction, multiplication)
            if(operation == 1)
                cout << firstValue << " + " << secondValue;
            else if(operation == 2)
                cout << firstValue << " - " << secondValue;
            else if(operation == 3)
                cout << firstValue << " x " << secondValue;

            // If user answered 10 questions correctly
            if(score == 10)
            {
                won = true;

                // Push all fish rightwards
                for(int i=0; i<35; i++)
                {
                    UpdateFishPositions( fishies );
                    ClearScreen();
                    DrawFishies( fishies );
                    usleep(100000);
                }

                ClearScreen();
                DrawSprite ({currentPosition.row, (currentPosition.col + 2)}, YOU_WIN); // Displays a celebratory message
            }

            // If user answers a question correctly
            if( currentCommand.compare(answer) == 0)
            {
                CreateFishie(fishies);      // New fish spawns
                score++;
            }

            // If user answers a question incorrectly (with an incorrect numerical value)     
            else if ( (currentCommand.find("0") == 0) or
            (currentCommand.find("1") == 0) or              // Although not very DRY code, this was
            (currentCommand.find("2") == 0) or              // used because issues arose when 
            (currentCommand.find("3") == 0) or              // currentCommand was converted to an int
            (currentCommand.find("4") == 0) or
            (currentCommand.find("5") == 0) or
            (currentCommand.find("6") == 0) or
            (currentCommand.find("7") == 0) or
            (currentCommand.find("8") == 0) or
            (currentCommand.find("9") == 0))
            {
                alive = false;

                // Push all the fish downwards
                for(int i=0 ; i<10; i++)
                {
                    UpdateFishPositions( fishies );
                    ClearScreen();
                    DrawFishies( fishies );
                    usleep(100000);                  // sleep 0.1 seconds to allow the fish to move slower
                }

                ClearScreen();
                DrawSprite ({currentPosition.row, (currentPosition.col + 2)}, YOU_LOST);      
            }

            else if ( currentCommand.compare("replay") == 0)
            {
                ClearScreen();
                fishies.clear();        // Erases all the fish so when you start a new game, it starts from 0 fish
                score = 0;
                alive = true;
                won = false;
                if(operation == 1)
                    cout << firstValue << " + " << secondValue;
                else if(operation == 2)
                    cout << firstValue << " - " << secondValue;
                else if(operation == 3)
                    cout << firstValue << " x " << secondValue;
                CreateFishie(fishies);
                DrawFishies(fishies);
            }

            // If the user quits the game
            else if( currentCommand.compare("quit") == 0)
            {
                ShowCursor();
                SetNonblockingReadState( false );
                TeardownScreenAndInput();
                cout << endl; // be nice to the next command
                ClearScreen();
                return EXIT_SUCCESS;
            }

            else if ( currentCommand.compare("") != 0)      // outputs an error and allows the user the chance to replay/quit
            {
                ClearScreen();
                MoveTo(10,0);
                cout << "Error: You typed an invalid command. How am I supposed to catch fish with that?" << endl;
                alive = false;
            }

            // Calculates question's correct answer
            if(operation == 1)
                answer = to_string(firstValue + secondValue);
            if(operation == 2)
                answer = to_string(firstValue - secondValue);
            if(operation == 3)
                answer = to_string(firstValue * secondValue);

            if ( showCommandline )
            {
                MoveTo( 21, 1 );
                ShowCursor();
                if (alive and !(won))
                {
                    cout << "Answer: " << flush;
                }
                else
                {
                    cout << "Play again? (type \"quit\" to end the game or \"replay\" to play again!): " << flush;
                }

            }
            else { HideCursor(); }

            // Clear inputs in preparation for the next iteration
            startTimestamp = endTimestamp;
            currentChar = NULL_CHAR;
            currentCommand.clear();
        }
        // Depending on the blocking mode, either read in one character or a string (character by character)
        if ( showCommandline )
        {
            while ( read( 0, &currentChar, 1 ) == 1 && ( currentChar != '\n' ) )
                cout << currentChar << flush; // the flush is important since we are in non-echoing mode
                currentCommand += currentChar;
            }
            currentChar = NULL_CHAR;
        }
        else {
            read( 0, &currentChar, 1 );
        }
    }