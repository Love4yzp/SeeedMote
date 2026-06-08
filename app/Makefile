# SeeedMote Retail Demo — dev shortcuts
#
# Usage:
#   make dev            install deps + start backend & frontend (Ctrl-C stops both)
#   make dev MOCK=true  same, but with scripted mock events (no hardware needed)
#   make install        install Python venv + npm deps only
#   make backend        start backend only
#   make frontend       start frontend only (assumes backend is already running)

BACKEND_DIR := backend
FRONTEND_DIR := app
VENV        := $(BACKEND_DIR)/.venv
PYTHON      := $(VENV)/bin/python

MOCK ?= false
BACKEND_PORT ?= 3001
FRONTEND_PORT ?= 5173
BACKEND_URL := http://localhost:$(BACKEND_PORT)

.PHONY: dev install backend frontend

dev: install
	@echo "Starting backend (:$(BACKEND_PORT)) + frontend (:$(FRONTEND_PORT)) — Ctrl-C to stop both"
	@trap 'kill 0' INT TERM; \
	  MOCK=$(MOCK) SEEEDMOTE_APP_PORT=$(BACKEND_PORT) $(PYTHON) $(BACKEND_DIR)/main.py & \
	  SEEEDMOTE_FRONTEND_PORT=$(FRONTEND_PORT) SEEEDMOTE_BACKEND_URL=$(BACKEND_URL) npm --prefix $(FRONTEND_DIR) run dev & \
	  wait

install:
	@[ -d $(VENV) ] || python3 -m venv $(VENV)
	@$(VENV)/bin/pip install -q -r $(BACKEND_DIR)/requirements.txt
	@npm --prefix $(FRONTEND_DIR) install --silent

backend:
	MOCK=$(MOCK) $(PYTHON) $(BACKEND_DIR)/main.py

frontend:
	npm --prefix $(FRONTEND_DIR) run dev
