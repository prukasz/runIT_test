# runIT
> **`YOU can't ruin IT`**

---

### Czy to projekt dla Ciebie?

* ✅ Chcesz widzieć działający pomysł w kilka minut?
* ✅ Chcesz zrobić prosty projekt IoT?
* ✅ Chcesz zbudować pojazd RC i dodać do niego funkcje, które dotąd wydawały się skomplikowane?
* ✅ Chcesz zautomatyzować swój projekt i dodać elementy niewspierane przez standardowe rozwiązania RC?

**Jeśli tak – runIT jest dla Ciebie.**

---

## 💡 Czym jest runIT?

**runIT** to kontroler łączący pogranicze **IoT**, modelarstwa **RC** oraz **automatyki**. 

Pozwoli Ci on na podłączenie standardowych elementów RC (takich jak serwa, ESC, różnego rodzaju aktuatory) oraz łatwą integrację popularnych czujników i pasków LED. Dzięki aplikacji mobilnej stworzysz interfejs sterowania, a za pomocą prostego języka graficznego uczynisz swój projekt "SMART" i podłączysz go do sieci.

---

## ⚡ Co oferujemy? (Hardware)

### 🔌 Zasilanie i Smart IO
System zasilania zaprojektowany dla wymagających rozwiązań:
* **Wszechstronne źródła:** Zasilanie z akumulatora, **USB-C PD** lub zewnętrznego zasilacza.
* **Regulacja napięcia:** Dostępne **5V** i **3.3V** oraz konfigurowalne źródło napięcia.
* **Smart IO:** Inteligentne wejścia/wyjścia pozwalające na:
    * Zasilanie elementów bezpośrednio z portu.
    * **Pomiar prądu i napięcia** na każdym z wejść.
    * Wbudowane **zabezpieczenie** przed przeciążeniem oraz zwarciem.


### 📡 Komunikacja i Sterowanie
* **Łączność:** WiFi oraz BLE (Bluetooth Low Energy).
* **PWM:** Dedykowane wyjścia z zasilaniem 5V do obsługi wielu serw modelarskich.
* **Interfejsy:** Porty **I2C** oraz **SPI** do obsługi czujników i modułów.
* **GPIO:** Bezpośrednie wyjścia z mikrokontrolera dla przyszłej rozbudowy.
* **Czujniki:** Wbudowany **żyroskop** i **akcelerometr**.

### 🔮 Plany na daleką przyszłość
* **Range Extender:** Możliwość wydłużenia zasięgu poprzez dodatkowy moduł – podepnij pada do aplikacji, wepnij moduł do telefonu i steruj swoim projektem na duże odległości.

---

## 📱 Aplikacja i Język Programowania

Aplikacja mobilna umożliwia łatwą konfigurację podpiętych elementów i "ożywienie" projektu za pomocą prostego, graficznego języka blokowego.

### Cechy środowiska:
* **Hybrid Logic:** Język typu **Flow-Based** połączony z elementami wizualnymi znanymi z **PLC (FBD)**.
* **Visual Programming:** Proste, wizualne łączenie bloków.
* **Integracja RC:** Łatwe łączenie logiki z konfigurowalnym "pilotem RC" w aplikacji.
* **IoT Ready:** Bloki do komunikacji sieciowej (np. przez **MQTT**).
* **Live Debug:** Możliwość podglądu działania programu na żywo.
* **OTA** Wgrywanie kodu bezpośrednio przez aplikację.
* **Custom Blocks:** Możliwość tworzenia własnych bloków w języku **C** (z wykorzystaniem przygotowanego API).
* **Low-Level** Bloki I2C oraz SPI do tworzenia prostej komunikacji z nowymi układami.

---

## ⚠️ Czego NIE oferujemy

Aby uniknąć nieporozumień, warto wyjaśnić czym runIT **nie jest**:

1.  ❌ **To nie MicroPython ani Arduino:** Nie oferujemy dostępu do całego ekosystemu tych platform.
2.  ❌ **To nie "Bare Metal":** Zaimplementowany język nie pozwala na implementację bardzo złożonych algorytmów obliczeniowych ani na bezpośredni dostęp do warstwy HAL poza udostępnionym API.
3.  ❌ **Ograniczenia skryptowe:** Dodawanie własnych bloków zazwyczaj ogranicza się do prostych skryptów logicznych.
4.  ❌ **To nie przemysłowe PLC:** Choć pewne elementy (jak styl programowania) są wspólne, runIT nie jest certyfikowanym sterownikiem przemysłowym do zastosowań krytycznych.


## 🗺️ Roadmap

Poniżej znajduje się aktualny stan prac nad projektem runIT:

- 🏗️ Zmienne w przestrzeni języka
- 🏗️ Ustalenie ograniczeń i potrzeb wyjść kontrolera
- 🏗️ Podstawowa komunikacja BLE
- 🏗️ Silnik logiczny 
- 🏗️ API dla bloków 
- 🏗️ Postawowe bloki
- 🏗️ Bloki I2C 
- 🏗️ Dobór konkretnych rozwiązań 
- 🏗️ Live debugging i error-handling
- 🏗️ Python: środowisko testowe
- [x] Implemetacja bloków GPIO
- [x] Implementacja danych systemowych ( napięcie, prąd poszeczególnych ścierzek)
- [x] Implementacja bloku SPI 
- [x] Dodanie wsparcia do JSON oraz stringów (i bloków do nich) 
- [x] Projekt PCB
- [x] Implementacja komunikacji WiFi
- [x] Aplikackja mobilna
- [x] Integracja z Home Assistant (MQTT) 
- [x] **Range Extender** (Moduł dalekiego zasięgu)