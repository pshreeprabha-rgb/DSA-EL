.PHONY: backend-test eval server frontend-install frontend-test check

backend-test:
	$(MAKE) -C backend test

eval:
	$(MAKE) -C backend eval

server:
	$(MAKE) -C backend server

frontend-install:
	cd frontend && npm ci

frontend-test:
	cd frontend && npm test

check: backend-test frontend-test

