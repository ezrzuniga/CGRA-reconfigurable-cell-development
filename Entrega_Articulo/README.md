Entrega: artículo preliminar (LaTeX)

Archivos:
- article_prelim.tex: fuente LaTeX del artículo preliminar.
- references.bib: fichero BibTeX con referencias de ejemplo.

Compilación (recomendado con `latexmk` o `pdflatex` + `bibtex`):

Usando latexmk:

```bash
cd CGRA-reconfigurable-cell-development/Entrega_Articulo
latexmk -pdf article_prelim.tex
```

Manual (si no tiene latexmk):

```bash
cd CGRA-reconfigurable-cell-development/Entrega_Articulo
pdflatex article_prelim.tex
bibtex article_prelim
pdflatex article_prelim.tex
pdflatex article_prelim.tex
```

Si desea, puedo intentar compilar el PDF aquí y proporcionarlo.
