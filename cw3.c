//Mastermind board game on Raspberry pi with LED's and Button
//Created by Mark Gordon and Calum McLean


#include <sys/time.h>		/* for setitimer */
#include <unistd.h>		/* for pause */
#include <signal.h>		/* for signal */
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <stdbool.h>

//==================================================================

int buttonPresses[3];
int buttonTurn =0;
static volatile unsigned int gpiobase = 0x3F200000 ;
static volatile uint32_t *gpio ;

//=================================================================

void getButtonInput(void);

//=============================================================


void mmapfunc(){

  int   fd ;

	// memory mapping
  // Open the master /dev/memory device

  fd = open ("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);

  // GPIO:
  gpio = (uint32_t *)mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpiobase) ;

}


//============================================================



void toggleLED(int pin, int mode){

  int shift = pin * 3;


  asm(

    "MOV R1, %[gpio] \n\t"      //move gpio address into R1
	  "ADD R1, #0 \n\t"           //add register value (always #0 in this program)
	  "LDR R2, [R1, #0] \n\t"    //load R2 with bit string of address in R1
	  "MOV R3, #1 \n\t"          //move 1 into R3
	  "LSL R3, %[shift] \n\t"    //perform left shift on R3 by value of shift
	  "ORR R2, R2, R3  \n\t"     //perform OR operation on R2 and R3
	  "STR R2, [R1, #0] \n\t"    //store result to set up required pin


	  "MOV R1, %[gpio] \n\t"      //move gpio address into R1
	  "ADD R1, %[mode] \n\t"      //add mode to gpio for appropriate register (28 for gpset0, 40 for gpclr0)
	  "MOV R3, #1 \n\t"           //move 1 into R3
	  "LSL R3, %[pin]  \n\t"     //perform left shift on R3 by value of pin
	  "ORR R2, R2, R3  \n\t"     //OR operation performed on R2 and R3
	  "STR R2, [R1, #0] \n\t"    //store result to toggle LED
	  :
	  : [gpio] "r" (gpio), [mode] "r" (mode), [pin] "r" (pin), [shift] "r" (shift)
	  : "r1", "r2", "r3"

  );

}


int checkButton(){

  int buttonOnValue, buttonCurrentValue;


  asm(

    "\tMOV	R1, %[gpio]\n"      //move gpio address into R1
    "\tADD	R1, R1, #52\n"      //add 52 bytes to R1 to get gplev0 address
    "\tLDR  R2, [R1]\n"         //load R2 with bit string of address in R1
    "\tMOV  %1, R2\n"           //store current button value in c variable
    "\tORR  %0, R2, #1<<19 \n"  //store button on value in c variable

    : "=r" (buttonOnValue), "=r" (buttonCurrentValue)
    : [gpio] "r" (gpio)
    : "r1", "r2"

  );

  //return 1 if both values are equal, 0 if not

  int equality = (buttonOnValue == buttonCurrentValue);

  return equality;

}

//=================================================================

void setUpTimer(int interval){

  struct itimerval it_val;  /* for setting itimer */

  /* Upon SIGALRM, call getButtonInput().
   * Set interval timer.  We want frequency in ms,
   * but the setitimer call needs seconds and useconds. */
  if (signal(SIGALRM, (void (*)(int)) getButtonInput) == SIG_ERR) {
    perror("Unable to catch SIGALRM");
    exit(1);
  }
  it_val.it_value.tv_sec =     interval/1000;
  it_val.it_value.tv_usec =    (interval*1000) % 1000000;
  it_val.it_interval = it_val.it_value;
  if (setitimer(ITIMER_REAL, &it_val, NULL) == -1) {
    perror("error calling setitimer()");
    exit(1);
  }

}

void delay (unsigned int howLong){

  struct timespec sleeper, dummy ;

  sleeper.tv_sec  = (time_t)(howLong / 1000) ;
  sleeper.tv_nsec = (long)(howLong % 1000) * 1000000 ;

  nanosleep (&sleeper, &dummy) ;

}

//==================================================================


void flashLED(int pin, int noOfFlashes){

  //this method will flash the LED 'pin' for 'noOfFlashes' times

  int i;

  for(i=0; i < noOfFlashes; i++){

	  toggleLED(pin ,28);
	  delay(1000);
	  toggleLED(pin, 40);
    delay(1000);

  }

}

//==============================================================

int checkCorrect(int codecopy[]){

  //this method checks how many of the users inputs are correct and in the
  //right position and returns the result

  int x, correct=0;


  for(x=0; x < 3; x++){

		if(buttonPresses[x] == codecopy[x]){

		  correct++;
			codecopy[x] = -3;
			buttonPresses[x] = -4;

		}

	}


  return correct;


}

int checkApproximate(int codecopy[]){

  //this method checks how many of the users inputs are correct but NOT in the
  //the right positon and returns the result

  int x, approximate =0;


  for(x=0; x <3; x++){


		if(buttonPresses[x] == codecopy[0]){

			approximate++;
			codecopy[0] = -1;            //replaces with negative value to stop it being matched with a users input
			buttonPresses[x] = -2;       //replaces with negative value to stop it being matched with the code

		}

		if(buttonPresses[x] == codecopy[1]){

			approximate++;
			codecopy[1] = -1;
			buttonPresses[x] = -2;

		}

		if(buttonPresses[x] == codecopy[2]){

			approximate++;
			codecopy[2] = -1;
			buttonPresses[x] = -2;

		}

	}

  return approximate;

}


void getUserInput(){

  //this method handles getting the user input for guessing the code combination

  int k;
  for (k=0;k < 3;k++){

    printf("\nEnter input %d...\n\n", k+1);

    setUpTimer(500); //timer for 0.5 secs
    pause();         //and pauses calling the getButtonInput() method

  	if(buttonPresses[k] > 0 && buttonPresses[k] < 4){ //if users input is valid (1-3)

      flashLED(5, 1);
      flashLED(6, buttonPresses[k]); //flash LED 6 for users input

  	}else{ //reloop if invalid input

  	  printf("Re-enter input\n\n");
      printf("Invalid input : %d  (Must be between 1 - 3)\n", buttonPresses[k]);
  	  k--;
      buttonTurn--;

    }

  }


}

void correctCombination(){

  printf("Well done! You have entered the correct combination!\n\n");
  toggleLED(5, 28);
  flashLED(6, 3);
  toggleLED(5, 40);

}

void userFeedback(int correct, int approximate){

  //this method gives feedback to user for how many correct inputs they have,
  //and correct positions

  printf("%d inputs had the correct input and position\n", correct);

  flashLED(6, correct); //flash for number of positions user got correct
  delay(1000);
  flashLED(5, 1);
  delay(1000);

  printf("%d inputs had the correct input but the wrong position\n\n", approximate);

  flashLED(6, approximate); //flash for number of times user approximate got correct (positional problem)

}


void endOfRound(){

  //this method signals end of the round and displays users inputs

  printf("==END OF ROUND==\n\n");
  flashLED(5, 2);
  delay(1000);

  printf("Inputs : %d, %d, %d\n\n", buttonPresses[0], buttonPresses[1], buttonPresses[2]);

}

//============================================================

int main(int argc, const char* argv[]){

  int correct =0;
	int approximate =0;

  mmapfunc();        //maps gpio address



  if (geteuid () != 0)
    fprintf (stderr, "setup: Must be root. (Did you forget sudo?)\n") ;



  srand(time(NULL));      //create random code

	int code[3];

	int y;
	for(y=0; y < 3; y++)
		code[y] = (rand() % 3) + 1;


  //This can be uncommented for testing purposes
	//printf("random numbers are: %d %d %d\n\n", code[0], code[1], code[2]);



  while(1){

    printf("==ROUND START==\n\n");

    delay(2000);

    getUserInput();    //gets the users button input

    endOfRound();     //signals end of round to user and displays input

    //copy of code is made so when changes are made mid-round, it won't effect
    //the next round
	  int codecopy[3];

	  int x;
	  for(x=0; x < 3; x++)
		  codecopy[x] = code[x];


	  correct = checkCorrect(codecopy);   //returns number of correct inputs

    //signals user they have got the code correct and then breaks
	  if(correct == 3){
      correctCombination();
		  break;
	  }

    printf("Incorrect combination\n\n");

	  approximate = checkApproximate(codecopy);  //returns number of correct inputs, but NOT in the correct position

    userFeedback(correct, approximate);  //gives feedback to user on their performance

  //resets variables for next round and flashes LED to signify end of round to user

  buttonPresses[0]=buttonPresses[1]=buttonPresses[2]=0;

  buttonTurn = correct = approximate = 0;

  flashLED(5, 3);

  }

}

//===============================================================

void getButtonInput(void) {

  setUpTimer(0);                //timer is stopped to prevent method being called
                                //again before its finished

  buttonPresses[buttonTurn] = 0;  //this is the number of button presses for each input turn

  int j;
  for(j=0; j < 100; j++){

	  int valreturned = checkButton();     //reads button, 1 returned if pressed, 0 if not

	  buttonPresses[buttonTurn] =  buttonPresses[buttonTurn] + valreturned;  //adds to total

    while(checkButton() == 1){};      //will stall if user keeps button held down
                                      //prevents multiple inputs on same click

    delay(30);
  }

  printf("Input: %d\n\n",  buttonPresses[buttonTurn]);   //informs user of their input

  buttonTurn = buttonTurn + 1;                        //increments turn

}
