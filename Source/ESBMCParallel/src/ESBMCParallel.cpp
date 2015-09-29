/*
 #
 # ESBMC - Parallel Runner
 #
 #               Universidade Federal do Amazonas - UFAM
 # Author:       Hussama Ismail <hussamaismail@gmail.com>
 #
 # ------------------------------------------------------
 #
 # Execute many esbmc instances using OpenMP Library
 #
 # Usage Example:
 # $ ./esbmc-parallel filename hmax
 #
 # ------------------------------------------------------
 #
 */

#include <iostream>
#include <omp.h>
#include <string>
#include <string.h>
#include <stdio.h>
#include <ctime>
#include <vector>
#include <time.h>
#include <cstdlib>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <sstream>
#include <algorithm>
#include <fcntl.h>
#include <poll.h>

#include "ParallelController.h"

#define READ 0
#define WRITE 1

enum Order { ASC, DESC };

std::time_t start;
std::string executable = "./esbmc";
std::string filename;
std::string esbmc_parameters = "--boolector --quiet";

pid_t popen2(const char *command, int *infp, int *outfp) {
	int p_stdin[2], p_stdout[2];
	pid_t pid;

	if (pipe(p_stdin) != 0 || pipe(p_stdout) != 0)
		return -1;

	pid = fork();

	if (pid < 0)
		return pid;
	else if (pid == 0) {
		setpgid(0, 0);

		dup2(p_stdin[READ], STDIN_FILENO);
		dup2(p_stdout[WRITE], STDOUT_FILENO);

		//close unuse descriptors on child process.
		close(p_stdin[READ]);
		close(p_stdin[WRITE]);
		close(p_stdout[READ]);
		close(p_stdout[WRITE]);

		//can change to any exec* function family.
		execl("/bin/bash", "bash", "-c", command, NULL);
		perror("execl");
		exit(1);
	}

	// close unused descriptors on parent process.
	close(p_stdin[READ]);
	close(p_stdout[WRITE]);

	if (infp == NULL)
		close(p_stdin[WRITE]);
	else
		*infp = p_stdin[WRITE];

	if (outfp == NULL)
		close(p_stdout[READ]);
	else
		*outfp = p_stdout[READ];

	return pid;
}

int pclose2(FILE * fp, pid_t pid) {
	int stat;

	fclose(fp);
	while (waitpid(pid, &stat, 0) == -1) {
		if (errno != EINTR) {
			stat = -1;
			break;
		}
	}

	return stat;
}

void writeLog(int core, std::string log) {
	std::time_t end = std::time(NULL);
	long time = end - start;

	std::cout << std::to_string(time) + " > C" + std::to_string(core) + ": " + log << std::endl;
}

std::string execute_cmd(std::string command) {
	FILE* pipe = popen(command.c_str(), "r");
	if (!pipe)
		return "ERROR";
	char buffer[128];
	std::string result = "";
	while (!feof(pipe)) {
		if (fgets(buffer, 128, pipe) != NULL)
			result += buffer;
	}
	pclose(pipe);
	return result;
}

std::string execute_cmd_with_abort(ParallelController* controller, int core, int step, std::string command) {
	int status;
	int fd;
	pollfd pollfd[1];
	pid_t pid = popen2(command.c_str(), NULL, &fd);

	int flags;
	flags = fcntl(fd, F_GETFL, 0);
	flags |= O_NONBLOCK | FASYNC;
	fcntl(fd, F_SETFL, flags);

	pollfd[0].fd = fd;
	pollfd[0].events = POLL_IN;

	char buffer[128];
	std::string result = "";
	while (1) {

		poll(pollfd, 1, 1000);

		if (pollfd[0].revents & POLLHUP) //EOF
			break;
		else if (pollfd[0].revents & POLLIN) { //read
			while (read(fd, buffer, 128) > 0)
				result += buffer;
		}

		if (!controller->isStepNeeded(step)) {
			writeLog(core, "Abort Step " + std::to_string(step));

			kill(-pid, SIGKILL);

			while ((waitpid(-1, &status, WNOHANG)) > 0)
				sleep(1);

			break;
		}
	}

	close(fd);

	return result;
}

void execute_binary_search(int hmin, int hmax, int cores) {
	std::vector<int> founds;

	ParallelController controller(hmin, hmax);

#pragma omp parallel for
	for (int thread = 0; thread < cores - 1; thread++) {

		while (true) {
			int step = 0;

#pragma omp critical
			step = controller.getNextStep();

			if (step == 0)
				break;

			writeLog(thread, "Started Step " + std::to_string(step));

			std::string command_line = executable + " " + filename + " " + esbmc_parameters + " -Dvalordeh=" + std::to_string(step);
			std::string result = execute_cmd_with_abort(&controller, thread, step, command_line);

			std::size_t verification_failed = result.find("VERIFICATION FAILED");

			if (verification_failed != std::string::npos) {
				founds.push_back(step);
				writeLog(thread, "Finished Step " + std::to_string(step) + " > False");

#pragma omp critical
				controller.setStepResult(step, false);
			} else {
#pragma omp critical
				controller.setStepResult(step, true);
				writeLog(thread, "Finished Step " + std::to_string(step) + " > True");
			}
		}
	}

	if (founds.size() > 0) {
		int cLower = *std::min_element(founds.begin(), founds.end());

		time_t end = std::time(NULL);
		long time = end - start;

		std::cout << std::endl;
		std::cout << "The best solution is: " << std::to_string(cLower)
				<< " in " << time << "s" << std::endl;
	} else {
		std::cout << std::endl;
		std::cout << "No solution found:(" << std::endl;
	}
}

void execute_sequential_opt_search(int hmin, int hmax, int cores, Order order) {
	std::vector<int> founds;
	bool stop = false;

	if(order == DESC)
		hmin = hmax + 1;
	else
		hmin--;

#pragma omp parallel for
	for (int thread = 0; thread < cores - 1; thread++) {

		while (true) {
			int step = 0;

			if(order == ASC) {
#pragma omp critical
				hmin++;
				step = hmin;
			}
			else {
#pragma omp critical
				hmin--;
				step = hmin;
			}

			if (step > hmax || step < hmin)
				break;

			if(stop)
				break;

			writeLog(thread, "Started Step " + std::to_string(step));

			std::string command_line = executable + " " + filename + " " + esbmc_parameters + " -Dvalordeh=" + std::to_string(step);
			std::string result = execute_cmd(command_line);

			std::size_t verification_failed = result.find("VERIFICATION FAILED");

			if (verification_failed != std::string::npos) {
				if(order == ASC)
					stop = true;

				founds.push_back(step);
				writeLog(thread, "Finished Step " + std::to_string(step) + " > False");
			} else {
				if(order == DESC)
					stop = true;

				writeLog(thread, "Finished Step " + std::to_string(step) + " > True");
			}
		}
	}

	if (founds.size() > 0) {
		int cLower = *std::min_element(founds.begin(), founds.end());
		//if(order == DESC)
		//	cLower--;

		time_t end = std::time(NULL);
		long time = end - start;

		std::cout << std::endl;
		std::cout << "The best solution is: " << std::to_string(cLower) << " in " << time << "s" << std::endl;
	} else {
		std::cout << std::endl;
		std::cout << "No solution found:(" << std::endl;
	}
}

void execute_sequential_search_worker(int step, std::vector<int>* founds, Order order) {
	writeLog(0, "Started Step " + std::to_string(step));

	std::string command_line = " { time " + executable + " " + filename + " " + esbmc_parameters + " -Dvalordeh=" + std::to_string(step) + "; } 2>> /dev/null";

	std::string result = execute_cmd(command_line);
	std::size_t verification_failed = result.find("VERIFICATION FAILED");
	if ( verification_failed != std::string::npos ){
		if(order == ASC)
			(*founds).push_back(step);

		writeLog(0, "Finished Step " + std::to_string(step) + " > False");
	} else {
		if(order == DESC)
			(*founds).push_back(step);

		writeLog(0, "Finished Step " + std::to_string(step) + " > True");
	}
}

void execute_sequential_search(int hmin, int hmax, int cores, Order order) {
	std::vector<int> founds;

	int current = hmin;
	int target = current + (cores - 2);

	if(order == DESC) {
		current = hmax;
		target = current - (cores - 2);
	}

	do {
		if(order == ASC) {
			#pragma omp parallel for
			for(int thread = current; thread <= target; thread++){
				execute_sequential_search_worker(thread, &founds, order);
			}
		} else {
			#pragma omp parallel for
			for(int thread = current; thread >= target; thread--){
				execute_sequential_search_worker(thread, &founds, order);
			}
		}

		if(order == ASC) {
			current = current + (cores - 2) + 1;
			target = current + (cores - 2);

			if(target > hmax)
				target = hmax;
		} else {
			current = current - (cores - 2) - 1;
			target = current - (cores - 2);

			if(target < hmin)
				target = hmin;
		}

		if (founds.size() > 0 ){
			time_t end = std::time(NULL);
			long time = end - start;

			int result = 0;
			if(order == ASC)
				result = *std::min_element(founds.begin(), founds.end());
			else {
				result = *std::max_element(founds.begin(), founds.end());
				result++;
			}

			std::cout << std::endl;
			std::cout << "The best solution is: " << std::to_string(result) << " in " << time << "s" << std::endl;
			break;
		}

	}while(target <= hmax && target >= hmin && current <= hmax && current >= hmin);

	if (founds.size() == 0 ){
		std::cout << std::endl;
		std::cout << "Not found a solution :(" << std::endl;
	}
}

void print_help_and_exit() {
	std::cout << "Wrong parameters" << std::endl;
	std::cout << "Usage:" << std::endl;
	std::cout << "./ESBMCParallel filename.c hmin hmax --method=(sequential|sequential_opt|binary) [--order=(asc|desc)]" << std::endl;
	exit(1);
}

typedef std::vector<std::string> Arguments;

int main(int argc, char *argv[]) {
	Arguments arguments(&argv[0], &argv[0 + argc]);

	if (arguments.size() < 5) {
		print_help_and_exit();
	}

	std::string method = arguments.at(4);
	method.erase(0, 9);

	if(method != "binary" && method != "sequential" && method != "sequential_opt") {
		std::cout << "Invalid Method: " << method << std::endl;
		print_help_and_exit();
	}

	Order orderEnum = ASC;

	if (arguments.size() >= 6) {
		std::string order = arguments.at(5);
		order.erase(0, 8);

		if(order != "asc" && order != "desc") {
			std::cout << "Invalid Order: " << order << std::endl;
			print_help_and_exit();
		}

		if(order == "desc")
			orderEnum = DESC;
	}

	start = std::time(NULL);

	filename = arguments.at(1);

	int hmin = std::atoi(arguments.at(2).c_str());
	int hmax = std::atoi(arguments.at(3).c_str());

	std::string esbmc_version = execute_cmd(executable + " --version");
	size_t nl_version = esbmc_version.find("\n");
	esbmc_version.replace(nl_version, std::string("\n").length(), "");

	std::string execution_date = execute_cmd("date");
	size_t nl_exdate = execution_date.find("\n");
	execution_date.replace(nl_exdate, std::string("\n").length(), "");

	std::string hardware = execute_cmd("echo \"CPU:$(cat /proc/cpuinfo | grep \"model name\" | tail -n1 | cut -d \":\" -f2) ($(cat /proc/cpuinfo | grep processor | wc -l) core(s)) ~ RAM: $(cat /proc/meminfo | grep \"MemTotal\" | cut -d \":\" -f2 | rev | cut -d \" \" -f 1,2 | rev)\"");
	size_t nl_hw = hardware.find("\n");
	hardware.replace(nl_hw, std::string("\n").length(), "");

	std::string core_number = execute_cmd("echo \"$(cat /proc/cpuinfo | grep processor | wc -l)\"");
	size_t nl_cn = core_number.find("\n");
	core_number.replace(nl_cn, std::string("\n").length(), "");
	int cores = std::atoi(core_number.c_str());

	std::cout << std::endl;
	std::cout << "*** ESBMC Parallel Runner v2.0 ***" << std::endl;
	std::cout << "Tool: ESBMC " << esbmc_version << std::endl;
	std::cout << "Date of run: " << execution_date << std::endl;
	std::cout << "Hardware: " << hardware << std::endl << std::endl;
	std::cout << "Log Format: [time(s)] > C[core]: [log string]" << std::endl;
	std::cout << "File: " << basename(filename.c_str()) << std::endl;
	std::cout << "Method: " << method.c_str() << std::endl;

	if(orderEnum == ASC)
		std::cout << "Order: Asc" << std::endl;
	else
		std::cout << "Order: Desc" << std::endl;

	std::cout << "RUNNING:" << std::endl;

	if(method == "binary")
		execute_binary_search(hmin, hmax, cores);
	else if(method == "sequential")
		execute_sequential_search(hmin, hmax, cores, orderEnum);
	else
		execute_sequential_opt_search(hmin, hmax, cores, orderEnum);
}
