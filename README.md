# Implementacja Algorytmu Huffmana

Kod źródłowy został napisany w języku C (standard C99) z naciskiem na wydajność. Plik ten skupia się na **istocie algorytmu**, pomijając warstwę optymalizacji (taką jak buforowane I/O, wielowątkowe zliczanie częstości czy obsługa błędów).

## 1. Zasada Działania (W skrócie)

Algorytm Huffmana to metoda bezstratnej kompresji danych. Jego główna idea polega na tym, aby częściej występujące znaki zapisywać za pomocą krótszych kodów bitowych, a rzadsze za pomocą dłuższych.

**Schemat działania:**

1.  **Zliczanie**: Policz ile razy występuje każdy znak w pliku.
    *   `A: 50, B: 10, C: 5`
2.  **Kolejka**: Ustaw znaki w kolejce od najrzadszego do najczęstszego.
3.  **Drzewo**: Łącz dwa najrzadsze elementy w parę, sumując ich wagi, aż powstanie jedno drzewo.
    *   `(C:5 + B:10) -> BC:15`
    *   `(BC:15 + A:50) -> ABC:65`
4.  **Kodowanie**: Przypisz bity ścieżkom w drzewie (lewo=0, prawo=1).
    *   `A` (krótka ścieżka) -> `0`
    *   `B` (długa ścieżka) -> `11`
    *   `C` (długa ścieżka) -> `10`

## 2. Struktury Danych

Podstawowym elementem jest węzeł drzewa Huffmana (`HuffNode`). Drzewo jest przechowywane w płaskiej tablicy (flat array):

```c
typedef struct {
    uint64_t weight; // Waga węzła (suma częstości wystąpień symboli w poddrzewie)
    int32_t left;    // Indeks lewego dziecka w tablicy węzłów (-1 jeśli brak)
    int32_t right;   // Indeks prawego dziecka w tablicy węzłów (-1 jeśli brak)
    int32_t symbol;  // Wartość symbolu (tylko dla liści, 0-255), -1 dla węzłów wewnętrznych
} HuffNode;
```

## 3. Budowanie Drzewa

Alegorytm ten wykorzystuje **Priority Queue** (zaimplementowaną jako Binary Heap `HuffHeap`), aby w każdym kroku łączyć dwa drzewa o najmniejszej wadze w jedno nowe drzewo.

Funkcja `huff_build_tree` realizuje ten proces:

```c
static int huff_build_tree(const uint64_t freq[HUFF_MAX_SYMBOLS], HuffNode *nodes, int *out_count) {
    HuffHeap heap = {0};
    // ... inicjalizacja sterty ...

    // KROK 1: Utwórz liście (Leaf Nodes) dla każdego występującego symbolu
    for (int symbol = 0; symbol < HUFF_MAX_SYMBOLS; ++symbol) {
        if (freq[symbol] == 0) continue; // Pomiń symbole, które nie występują w pliku
        
        nodes[count].weight = freq[symbol];
        nodes[count].left = -1;  // Liść nie ma dzieci
        nodes[count].right = -1;
        nodes[count].symbol = symbol;
        
        // Dodaj liść do Priority Queue
        heap_push(&heap, count);
        count += 1;
    }

    // KROK 2: Główna pętla redukcyjna
    // Dopóki na stercie jest więcej niż jeden element (czyli nie mamy jeszcze jednego korzenia)
    while (heap.size > 1) {
        // Pobierz dwa węzły o najmniejszej wadze (priorytecie)
        int a = heap_pop(&heap);
        int b = heap_pop(&heap);

        // Utwórz nowy węzeł wewnętrzny (Internal Node) będący rodzicem a i b
        nodes[count].weight = nodes[a].weight + nodes[b].weight; // Waga to suma wag dzieci
        nodes[count].left = a;
        nodes[count].right = b;
        nodes[count].symbol = -1; // Węzeł wewnętrzny nie reprezentuje konkretnego symbolu

        // Dodaj nowy węzeł z powrotem do Priority Queue
        heap_push(&heap, count);
        count += 1;
    }

    // Ostatni element pozostały na stercie to Root Node całego drzewa Huffmana
    return heap.data[0];
}
```

## 4. Generowanie Kodów (Tree Traversal)

Po zbudowaniu drzewa należy przypisać ciągi bitów każdemu symbolowi. Program robi to przechodząc rekurencyjnie przez drzewo od korzenia do liści (Depth First Search). Przejście w lewo oznacza dopisanie bitu `0`, a w prawo bitu `1`.

```c
static void huff_collect_codes_rec(const HuffNode *nodes, int node_index, HuffCode *codes, uint8_t *path, uint16_t depth) {
    const HuffNode *node = &nodes[node_index];

    // WARUNEK STOPU: Jeśli trafiliśmy do liścia (Leaf Node)
    if (node->left < 0 && node->right < 0) {
        HuffCode *code = &codes[node->symbol];
        code->bit_count = depth;
        // Przepisujemy dotychczasową ścieżkę (path) jako kod dla tego symbolu
        // ... (implementacja kopiowania bitów) ...
        return;
    }

    // Rekurencyjne przejście w lewo (dopisanie 0 do ścieżki)
    if (node->left >= 0) {
        path[depth] = 0;
        huff_collect_codes_rec(nodes, node->left, codes, path, depth + 1);
    }

    // Rekurencyjne przejście w prawo (dopisanie 1 do ścieżki)
    if (node->right >= 0) {
        path[depth] = 1;
        huff_collect_codes_rec(nodes, node->right, codes, path, depth + 1);
    }
}
```

## 5. Dekodowanie (Tree Traversal)

Proces dekodowania polega na odczytywaniu strumienia bitów i jednoczesnym poruszaniu się po drzewie Huffmana. Zaczynamy od korzenia (Root Node) i w zależności od odczytanego bitu idziemy w lewo lub w prawo, aż trafimy na liść (Leaf Node).

```c
    // Pętla dekodująca (uproszczona)
    while (produced < original_size) {
        int node_index = root; // Zaczynamy od korzenia drzewa
        
        // Dopóki aktualny węzeł nie jest liściem (ma dzieci)
        while (nodes[node_index].left >= 0 || nodes[node_index].right >= 0) {
            uint8_t bit = 0;
            // ... pobranie kolejnego bitu ze skompresowanego pliku ...
            
            // Jeśli bit to 1 idź w prawo, jeśli 0 idź w lewo
            node_index = bit ? nodes[node_index].right : nodes[node_index].left;
        }
        
        // Znaleziono liść - zawiera on odkodowany symbol
        unsigned char byte = (unsigned char)nodes[node_index].symbol;
        fputc(byte, out); // Zapisz symbol do pliku wyjściowego
        produced += 1;
    }
```