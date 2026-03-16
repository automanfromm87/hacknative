// SDL2 bindings for HackNative

// Core
function SDL_Init(int $flags): int;
function SDL_Quit(): void;

// Window
function SDL_CreateWindow(string $title, int $x, int $y, int $w, int $h, int $flags): string;
function SDL_DestroyWindow(string $window): void;

// Renderer
function SDL_CreateRenderer(string $window, int $index, int $flags): string;
function SDL_DestroyRenderer(string $renderer): void;
function SDL_SetRenderDrawColor(string $renderer, int $r, int $g, int $b, int $a): int;
function SDL_RenderClear(string $renderer): int;
function SDL_RenderPresent(string $renderer): void;
function SDL_RenderDrawPoint(string $renderer, int $x, int $y): int;
function SDL_RenderDrawLine(string $renderer, int $x1, int $y1, int $x2, int $y2): int;

// Timing
function SDL_Delay(int $ms): void;

// Events (runtime helper)
function hack_sdl_poll_quit(): int;
