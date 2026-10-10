# Conversione C17 per Apple Silicon: attività completate

Aggiornato alla tappa 15. Il progetto dispone di moduli e test nativi ARM64;
non dispone ancora di una build completa giocabile. “Verificato” qui indica i
percorsi coperti dai test, senza implicare equivalenza completa al gioco PS1.

1. **Ambiente C17/ARM64:** preset CMake/Ninja per macOS Apple Silicon, target
   isolati, warning severi e preset con AddressSanitizer/UndefinedBehaviorSanitizer.
2. **Memoria:** allocator mempack allineato, capacità e limiti controllati,
   aritmetica degli indirizzi compatibile con puntatori a 64 bit.
3. **Formati PS1:** separazione tra record binari e strutture residenti;
   letture little-endian/unaligned, campi scalari a larghezza fissa e risoluzione
   PTR senza sovrascrivere i file con puntatori host.
4. **Accesso agli asset:** BIGFILE/disco ISO, envelope DRAM, MPK/LEV/PTR e LNG,
   validazione dei limiti e uso del disco reale `assets/ctr-u.bin` nei test opzionali.
5. **Modelli e animazioni:** catalogo, intestazioni, definizioni di istanze,
   frame statici/compressi, decompressione e interpolazione, comandi dei triangoli,
   colori e riferimenti alle texture.
6. **Matematica e proiezione:** trasformazioni locali, matrici Q12, coordinate
   delle istanze, camera, GTE nativo e confronti con routine del gioco.
7. **VRAM e anteprime:** upload rettangolari, formati texture/CLUT/STP,
   raster software, immagini di Crash, sequenze animate e scene con istanze LEV.
   La camera sintetica e le anteprime diagnostiche non dimostrano parità visiva.
8. **Terreno e visibilità diagnostica:** mesh/BSP/PVS, selezione dei quad,
   clipping sui sei piani, materiali e subdivision uniforme nelle anteprime.
9. **Animazioni ambientali:** acqua e scenery con copie dei vertici,
   immutabilità della sorgente e confronti con AnimateWater/GTE di produzione.
10. **Workspace host:** collisioni, camera, Torch, skid e ombre; riferimenti
    completi, visitatori di puntatori e verifiche di rilocazione isolate.
11. **Strutture runtime:** CameraDC, PushBuffer, Driver/BotData, Instance,
    InstDrawPerPlayer e Level con riferimenti/callback a larghezza host,
    array flessibili allineati e dimensionamento dei pool.
12. **Collegamenti delle istanze:** peer Instance/InstDef, liste intrusive/PVS,
    inizializzazione tramite campi tipizzati e test dei viewport/frustum reali.
13. **Decodifica residente:** conversione dei record Level/Model/ModelHeader,
    animazioni, mesh/quad/BSP/hitbox, acqua/scenery, NAV, skybox e spawn.
14. **Renderer di produzione:** RenderBucket e cache di 256 vertici,
    indirizzi OT dei pneumatici normali/riflessi, cicli delle texture;
    workspace del terreno per 1–4 giocatori, ricorsione retail limitata,
    midpoint terra/acqua/full-dynamic, primitive GT3/GT4, clip e token OT.
15. **Loader residente:** grafi completi MPK/LEV/modello, oggetti condivisi,
    array e payload inline, pubblicazione dopo validazione, callback reali,
    PTR separati, proprietà degli asset legata ai mempack e ponte verso le viste
    binarie. La coda nativa conserva tutto lo slot, inclusi i callback a 64 bit.
16. **Snapshot degli asset:** rilocazione dei riferimenti dichiarati, ripristino
    di grafi con riferimenti reciproci, verifiche di ABI/dimensioni e rifiuto
    transazionale dei dati invalidi. Il checkpoint dell'intero gioco resta da
    integrare con questi nuovi oggetti heap.
17. **Verifica continua:** 32 test CTest superati sia normalmente sia con
    ASan/UBSan, inclusi gli asset reali; audit ARM64 riproducibile e documentazione.

## Cosa manca per giocare

- **Completare l'integrazione dei contratti runtime:** globali e overlay
  rimanenti, accessi tramite offset, trasporti di puntatori e visitatori del
  checkpoint dell'intero gioco. L'audit corrente conta 307 guardie di layout,
  290 diagnostiche di cast puntatore/intero e 39 siti scratchpad; molti sono
  collegati fra loro e non equivalgono a singoli task.
- **Compilare e avviare l'eseguibile ARM64 completo:** chiudere i contratti
  precedenti, collegare i moduli e verificare il caricamento del menu e della gara.
  I guardrail CMake e del layout globale sono tuttora attivi.
- **Rendere la prima gara effettivamente giocabile:** controlli/input,
  simulazione, collisioni, camera, GPU e audio con gli asset reali; correggere
  crash e divergenze, verificare i cicli di caricamento/scaricamento.
- **Stabilizzare le altre modalità:** multiplayer, adventure/cutscene,
  salvataggi/replay e confronto del rendering. Questa fase può seguire una prima
  gara funzionante, ma serve per parlare di port completo.

Sono ancora più macrofasi, non una singola compilazione finale. Non è possibile
ricavare una percentuale o una scadenza attendibile dal numero di guardie residue:
la prima stima temporale utile richiede l'avvio del loop reale del gioco.

## Test ripetibili

```sh
cmake --preset macos-arm64-memory
cmake --build --preset macos-arm64-memory
ctest --preset macos-arm64-memory
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
```

I test non avviano ancora una gara. Dettagli e limiti sono in [ARM64_PORT.md](ARM64_PORT.md).
