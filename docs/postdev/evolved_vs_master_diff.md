# Evolved Irssi vs Master Irssi - Szczegółowe Porównanie Funkcjonalności

## 🚀 System Sidepaneli (Główna Innowacja)

**Opis**: Kompletny system paneli bocznych z modularną architekturą i inteligentnym zarządzaniem.

- **Modularność**: Oddzielne moduły dla core, layout, rendering, activity i signals
- **Inteligentne Redrawing**: Granularne odświeżanie tylko lewego/prawego panelu zamiast całego ekranu
- **Batching Mass Events**: Hybrydowy system grupowania dla masowych join/part (400+ użytkowników)
- **Multi-Server Support**: Alfabetyczne sortowanie serwerów z właściwymi tagami serwerów
- **Kicked Channel Preservation**: Zachowanie etykiet kanałów po wykopaniu z maksymalnym priorytetem

## 🖱️ System Gestów Myszy (Mouse Gestures)

**Opis**: Intuicyjna nawigacja między oknami IRC poprzez przeciąganie myszy w obszarze czatu.

- **4 Typy Gestów**: Krótkie/długie przeciągnięcia w lewo/prawo z konfigurowalnymi akcjami
- **Inteligentne Rozpoznawanie**: Aktywne tylko w obszarze czatu, nie w sidepanelach
- **SGR Protocol**: Zaawansowane śledzenie ruchu myszy z precyzyjną detekcją drag events
- **Optimized Defaults**: Domyślne mapowania dostosowane do typowych wzorców użycia IRC
- **Configurable Sensitivity**: Regulowana czułość i timeout dla gestów

## 🎨 Enhanced Nick Display System

**Opis**: Zaawansowany system formatowania nicków z wyrównaniem, obcinaniem i kolorowaniem.

- **Dynamic Alignment**: Inteligentne wyrównywanie z konfigurowalnymi kolumnami
- **Smart Truncation**: Obcinanie długich nicków ze wskaźnikiem "+" 
- **Hash-Based Coloring**: Konsystentne kolory per nick per kanał z konfigurowalnymi paletami
- **Mode Color Separation**: Oddzielne kolory dla statusów użytkowników (op/voice/normal)
- **Real-Time Updates**: Wszystkie formatowania aplikowane dynamicznie podczas wyświetlania

## 📺 Whois w Aktywnym Oknie

**Opis**: Wyświetlanie odpowiedzi whois bezpośrednio w aktywnym oknie czatu zamiast w osobnym.

- **Context Preservation**: Brak przełączania kontekstu podczas sprawdzania whois
- **Improved Workflow**: Płynniejszy przepływ pracy podczas rozmów
- **Backward Compatible**: Możliwość wyłączenia i powrotu do standardowego zachowania
- **Clean Integration**: Naturalne wkomponowanie w istniejący interfejs
- **Message Level Filtering**: Właściwe poziomy wiadomości dla różnych typów odpowiedzi

## 🪟 Inteligentne Zarządzanie Oknami

**Opis**: Automatyczne tworzenie okien Notices i separatorów serwerów dla lepszej organizacji.

- **Notices Window**: Okno #1 programowo ustawiane jako "Notices" z level `NOTICES|CLIENTNOTICE|CLIENTCRAP|CLIENTERROR`
- **Auto Server Windows**: Automatyczne tworzenie okien statusu serwera przy połączeniu z level `ALL -NOTICES -CLIENTNOTICE -CLIENTCRAP -CLIENTERROR`
- **Message Separation**: Komunikaty systemowe (help, błędy, output komend) trafiają do Notices, ruch IRC do okien serwerów
- **Sidepanel Separators**: Okna serwerów służą jako wizualne separatory w lewym sidepanelu z właściwymi servertag
- **Immortal & Sticky**: Notices jest nieśmiertelne, okna serwerów są sticky do odpowiednich połączeń

## 🎯 Nowa Komenda /window lastone

**Opis**: Dodanie brakującej komendy do przechodzenia do faktycznie ostatniego okna na liście.

- **Missing Functionality**: Komenda która dziwnie nie istniała w oryginalnym irssi mimo oczywistej potrzeby
- **Gesture Integration**: Kluczowa dla systemu gestów myszy (długi gest w prawo)
- **True Last Window**: Przenosi do okna z najwyższym refnum, nie ostatnio aktywnego jak `/window last`
- **IRC Workflow**: Idealna dla szybkiego przeskakiwania na koniec listy okien gdzie często jest aktywność
- **Consistent Behavior**: Zachowuje się przewidywalnie - zawsze idzie do okna o najwyższym numerze

## 🎯 Perfect UTF-8 & Emoji Support (v0.0.7)

**Opis**: Kompleksowa implementacja obsługi UTF-8 z idealnym wyświetlaniem emoji w nowoczesnych terminalach.

- **Unified Grapheme Logic**: Identyczna logika grupowania grafemów w input field i chat window
- **Variation Selector Mastery**: Perfekcyjna obsługa emoji z selektorami wariantów (❣️, ♥️)
- **Chat Window Fix**: Całkowite wyeliminowanie overflow emoji z okna czatu do sidepaneli
- **Modern Terminal Support**: Natywne wsparcie dla Ghostty bez trybu legacy
- **Enhanced string_advance**: Rozszerzona funkcja `string_advance_with_grapheme_support()` z logiką specjalną dla emoji
- **Consistent Width Calculation**: Ujednolicone obliczanie szerokości między systemami input i display
- **Zero Breaking Changes**: Wszystkie istniejące funkcjonalności zachowane i ulepszone

## ⚡ Performance Optimizations

**Opis**: Optymalizacje wydajności skupione na redukcji niepotrzebnych operacji odświeżania.

- **Granular Redraws**: Funkcje `redraw_left_panels_only()` i `redraw_right_panels_only()`
- **Event-Specific Updates**: Aktualizacje tylko odpowiednich paneli dla konkretnych zdarzeń
- **Batch Processing**: Grupowanie masowych zdarzeń z timer fallback i immediate sync triggers
- **Safety Checks**: Zabezpieczenia przed błędami podczas tworzenia/niszczenia okien
- **Debug Logging**: System logowania debug z wyjściem do pliku dla diagnostyki

## 🔧 Enhanced Build System

**Opis**: Nowoczesny system budowania z automatycznym zarządzaniem zależnościami.

- **Meson + Ninja**: Szybkie, niezawodne buildy z kompleksowym zarządzaniem zależnościami
- **Cross-Platform**: Natywne wsparcie dla macOS i dystrybucji Linux
- **Auto-Installation**: Automatyczne wykrywanie i instalacja wszystkich wymaganych pakietów
- **Feature Complete**: Perl scripting, OTR messaging, UTF8proc, SSL/TLS out of the box
- **Dual Mode**: Instalacja jako `irssi` (zamiana) lub `erssi` (niezależne współistnienie)

## 🎨 Premium Themes Collection

**Opis**: Kolekcja wysokiej jakości motywów z zaawansowanymi konfiguracjami statusbar.

- **Nexus Steel Theme**: Nowoczesny motyw cyberpunk z zaawansowanymi konfiguracjami
- **Enhanced Default**: Ulepszona wersja klasycznego motywu irssi z lepszą czytelnością
- **Colorless Theme**: Minimalistyczny motyw dla terminali z ograniczonym wsparciem kolorów
- **iTerm2 Integration**: Premium schemat kolorów `material-erssi.itermcolors`
- **Auto-Installation**: Automatyczne kopiowanie motywów do `~/.erssi/` przy pierwszym uruchomieniu

## 📋 Nowe Ustawienia /set

### Sidepanel System
```bash
/set sidepanel_left on                    # Włącz lewy panel (lista okien)
/set sidepanel_right on                   # Włącz prawy panel (lista nicków)
/set sidepanel_left_width 20              # Szerokość lewego panelu
/set sidepanel_right_width 16             # Szerokość prawego panelu
/set sidepanel_right_auto_hide on         # Auto-ukrywanie prawego panelu
/set sidepanel_debug off                  # Debug logging do /tmp/irssi_sidepanels.log
/set auto_create_separators on            # Automatyczne tworzenie okien separatorów
```

### Mouse Gestures System
```bash
/set mouse_gestures on                    # Włącz system gestów myszy
/set mouse_scroll_chat on                 # Włącz przewijanie myszy w czacie
/set gesture_left_short "/window prev"    # Komenda dla krótkiego gestu w lewo
/set gesture_left_long "/window 1"        # Komenda dla długiego gestu w lewo
/set gesture_right_short "/window next"   # Komenda dla krótkiego gestu w prawo
/set gesture_right_long "/window lastone" # Komenda dla długiego gestu w prawo
/set gesture_sensitivity 10               # Czułość gestów (piksele)
/set gesture_timeout 1000                 # Timeout gestów (milisekundy)
```

### Nick Display System
```bash
/set nick_column_enabled on               # Włącz system kolumn nicków
/set nick_column_width 12                 # Szerokość kolumny nicka
/set nick_hash_colors "2,3,4,5,6,7,9,10,11,12,13,14" # Paleta kolorów hash
```

### Whois Enhancement
```bash
/set print_whois_rpl_in_active_window on  # Whois w aktywnym oknie
```

## 🎨 Nowe Formaty Theme

### Sidepanel Formats
```
sidepanel_header = "%B*%B$0%N"                    # Nagłówki serwerów
sidepanel_item = "%W %W$0%N"                      # Normalne elementy
sidepanel_item_selected = "%g%w> %g%w$0%N"        # Wybrane elementy
sidepanel_item_nick_mention = "%M# %M$0%N"        # Wspomnienie nicka (priorytet 4)
sidepanel_item_query_msg = "%M+ %M$0%N"           # Wiadomość prywatna (priorytet 4)
sidepanel_item_activity = "%y* %y$0%N"            # Aktywność kanału (priorytet 3)
sidepanel_item_events = "%Go%N %G$0%N"            # Wydarzenia (priorytet 1)
sidepanel_item_highlight = "%R! %R$0%N"           # Podświetlenia (priorytet 2)
```

### Nick Status Formats (Dual-Parameter)
```
sidepanel_nick_op_status = "%Y$0%N%B$1%N"         # Operatorzy (złoty status, stalowy nick)
sidepanel_nick_voice_status = "%C$0%N%c$1%N"      # Voice (cyjan status, jasny cyjan nick)
sidepanel_nick_normal_status = "%w$0%N%w$1%N"     # Normalni (jasny szary dla widoczności)
```

## 🔧 Nowe Expandos

```
$nickalign    # Zwraca spacje wyrównujące dla kolumny nicka
$nicktrunc    # Zwraca obcięty nick ze wskaźnikiem "+" jeśli za długi
$nickcolored  # Zwraca nick z hash-based kolorowaniem
```

## 📝 Nowe Komendy

```bash
/nickhash shift [kanał]  # Ręczne przesunięcie kolorów nicków w kanale
/window lastone          # Przejdź do okna z najwyższym refnum (faktycznie ostatnie)
```

## ⚙️ Filozofia Konfiguracji

**Wszystkie nowe funkcjonalności są domyślnie włączone** - taki jest zamysł projektu Evolved Irssi. Każda funkcjonalność może zostać wyłączona przez odpowiednie ustawienie `/set`, ale domyślnie wszystko jest aktywne, aby użytkownik od razu doświadczył pełni możliwości.

**Zachowanie Kompatybilności**: Staraliśmy się zachować zgodność z zasadami panującymi w kodzie irssi, konwencjami theme oraz ogólną architekturą projektu. Wszystkie zmiany są implementowane jako rozszerzenia, nie modyfikacje istniejącej funkcjonalności.

## 🏗️ Architektura Modułów

### Sidepanel System Files
- `sidepanels-core.c` - Główna koordynacja i zarządzanie ustawieniami
- `sidepanels-render.c` - Logika renderowania z optymalizowanymi funkcjami redraw
- `sidepanels-activity.c` - Śledzenie aktywności i batch processing
- `sidepanels-signals.c` - Obsługa sygnałów zdarzeń IRC
- `sidepanels-layout.c` - Zarządzanie układem i pozycjonowaniem paneli

### Mouse System Files
- `gui-mouse.c` - Obsługa zdarzeń myszy i parser protokołu SGR
- `gui-gestures.c` - System gestów z detekcją drag i klasyfikacją ruchów

### Enhanced Features Files
- `fe-expandos.c` - Rozszerzone expandos dla formatowania nicków
- `module-formats.c` - Definicje domyślnych formatów theme

---

**Podsumowanie**: Evolved Irssi wprowadza 47 nowych ustawień, 9 nowych formatów theme, 3 nowe expandos i 1 nową komendę, zachowując 100% kompatybilność z oryginalnym irssi.
