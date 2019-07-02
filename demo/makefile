PREFIX=$(HOME)/dev
BINDIR=$(PREFIX)/bin
ROFF=$(BINDIR)/roff
PDF=$(BINDIR)/pdf
TSQL=$(BINDIR)/tsql
TBL=$(BINDIR)/tbl
SOELIM=$(BINDIR)/soelim

all: tsql.pdf

%.pdf: %.tr
	@echo "Generating $@"
	@$(SOELIM) < $< | $(TSQL) | $(TBL) | $(ROFF) -mtbl | $(PDF) -pa4 > $@

clean:
	@rm -f tsql.pdf

