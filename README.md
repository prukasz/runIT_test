freertos config -> timer dispatch form ISR
freertos config -> tickrate 1000Hz


#include <stdint.h>


typedef struct{
    uint8_t *t; //zapisuje które wejścia już zostały zaktualizowane w cyklu, jest aktualizowana przez blok poprzedni w stosunku do tego który ma wykorzsytywane wejścia
    //tablica wszystkich wejść które są do kopiowania
    //zmienne globalne jeśli są podpięte do tego bloczka, nie kopiować tylko pointer
    // id zmiennej ?
    // bool że się wykonał 
    // id bloku które będzie kolejością wykonania
} in_state;

typedef struct 
{
    //do którego bloku skopiować zmienne wyjściowe i guess id ?
    //zapsiuje do którego bloku przekazał wykoywanie 
} out_state;

//FF00 -> Start zmiennych
// xx * 11 -> ile zmiennych danych typów / header


// *11
// xx -> ile zmodyfikowanych danego typu 
// xx -> ID_ zmiennej czyli indeks w tableli
// xx * rozmiar zmiennej -> dane
// leci do kolejnego typu zmiennych dlatego *11

//FFFF -> Start kodu

// FF/FE ? -> start transmisji bloczku(bez segmentacji/z segmentacją)
// xxxxxxxx -> numer trasmijsi/nr bloku/długość danych | BLOCK ID
// xx -> Tag bloku                  | BLOCK ID
// xx -> start bloczku
// ile się zmieści -> dane
// FF/FE -> jest kolejne dane bloczku

// 0000 -> Koniec kodu