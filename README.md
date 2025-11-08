freertos config -> timer dispatch form ISR
freertos config -> tickrate 1000Hz

BLE INFO 

typedef enum{
    DATA_UI8,   /00
    DATA_UI16,  /01
    DATA_UI32,  /02
    DATA_I8,    /03
    DATA_I16,   /04
    DATA_I32,   /05  
    DATA_F,     /06
    DATA_D,     /07
    DATA_B      /08
}data_types_t;

Wysłanie ilości danych:
FFFF -> start sekwencji
FF00 xx xx xx xx xx xx xx xx xx -> ile zmnienych pojedyńczych (max 256, musi być jedna zmienna jakakolwiek w programie), w kolejnosci enum
FF01 xx ->typ zmiennej , xx -> rozmiar (tablice 1d, można powtarzać wiele różnych na pakiet) 
FF02 xx ->typ zmiennej , xx -> rozmiar1, xx -> rozmiar2 (tablice 2d), można powtarzać wiele różnych na pakiet
FF03 xx ->typ zmiennej , xx -> rozmiar1, xx -> rozmiar2, xx -> rozmiar3 (tablice 3d) można powtarzać wiele różnych na pakiet
EEEE -> stworz zmienne
(opjonalnie)

FF10 -> uzupełanianie tablic typu UI8 , xx index tablicy , xx index początkowy(spłaszczony) , xxxxxxxxxxxxx dane tablicy spłaszczonej
FF11 -> uzupełanianie tablic typu UI16 analogicznie 
(reszta FF1X)
EEEE -> uzupełnij zmienne

tablice są uzupełniane kolejno kolumnami 






