# Textbook

**Textbook** is a lightweight, classic-style text editor built for the Yekara Desktop Interface System (based on FVWM).  
It provides a minimal yet functional interface for editing text files, inspired by retro Unix/Motif applications.

---

## Features

- Multi-line text editing with scrollable area  
- Standard File menu: Open, Save, Quit  
- Standard Edit menu: Cut, Copy, Paste, Select All  
- Keyboard accelerators for all major actions:  
  - `Ctrl+O` – Open  
  - `Ctrl+S` – Save  
  - `Ctrl+Q` – Quit  
  - `Ctrl+X` – Cut  
  - `Ctrl+C` – Copy  
  - `Ctrl+V` – Paste  
  - `Ctrl+A` – Select All  
- Blocks accidental text insertion when using Ctrl+key combinations  
- Custom GZI icon with support for FVWM/Yekara

---

## Installation

### Dependencies

Make sure you have the following installed:

- X11 development libraries (`libX11-dev`)  
- Motif development libraries (`libxm4-dev` or equivalent)
- libyk (libYekara) for yekara-only features, such as gzi and theming support

---

### Compile

Clone or download the source and run:

```bash
gcc main.c -o textbook -lXm -lXt -lX11 -lyk