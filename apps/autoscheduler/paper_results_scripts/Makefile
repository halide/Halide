greedy:
	bash generate_apps_results.sh greedy
	@echo 
	@echo Greedy 
	@cat results/greedy/results.txt

beam_search:
	bash generate_apps_results.sh beam_search
	@echo 
	@echo Beam Search 
	@cat results/beam_search/results.txt

all:
	bash generate_apps_results.sh greedy
	bash generate_apps_results.sh beam_search
	@echo 
	@echo Greedy
	@cat results/greedy/results.txt
	@echo 
	@echo -------------------------
	@echo 
	@echo Beam Search 
	@cat results/beam_search/results.txt
