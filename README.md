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

Bloczki header 

bbxx (xx) (0,1) typ bloczku - header, xx numer pakietu(2) (00 -> tworzenie bloczku), xxxx (3,4) id bloczku,  xx in_cnt(5) , xx ,xx, xx ... in type , xx q_cnt, xx ,xx ,xx. q type,
następnie idzie lista połączeń wyść: 
xx in_cnt (q0)
xxxx - id bloczku xx- numer wejscia
razy in_cnt
xx in_cnt (q0)
xxxx - id bloczku xx- numer wejscia
razy in_cnt
xx in_cnt (q0)
xxxx - id bloczku xx- numer wejscia
razy in_cnt

przyklad: 
bb00 00 0000 02 01 07 03 01 07 07 03 01 0100 00 00 02 0400 01 0100 01 
bb00 00 0100 02 01 07 02 01 07 02 01 0200 00 01 0200 01 
bb00 00 0200 02 01 07 02 01 07 02 01 0300 00 01 0300 01 
bb00 00 0300 02 01 07 02 01 07 01 01 0400 00 
bb00 00 0400 02 01 07 02 01 07 00

blok uzupełniania zmienią globalną 

bbff ff xxxx, xx->ile xx->case xx->datatyp xx->id || 

(dostęp rekursywny)
bb00 FE 0400 XX target type, XX target idx, (xx, xx, xx static idxs), (XX target type, XX target idx, (xx, xx, xx static idxs))(idx 0 D1), FF (empty) , FF(exmpty) FF(empty), 









