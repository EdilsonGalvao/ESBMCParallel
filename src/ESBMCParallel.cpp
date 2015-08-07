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

/*FILE * popen2(std::string command, std::string type, int & pid)
 {
 pid_t child_pid;
 int fd[2];
 pipe(fd);

 if((child_pid = fork()) == -1)
 {
 perror("fork");
 exit(1);
 }

 // child process
 if (child_pid == 0)
 {
 setpgid(0, 0);

 if (type == "r")
 {
 close(fd[READ]);    //Close the READ end of the pipe since the child's fd is write-only
 dup2(fd[WRITE], 1); //Redirect stdout to pipe
 }
 else
 {
 close(fd[WRITE]);    //Close the WRITE end of the pipe since the child's fd is read-only
 dup2(fd[READ], 0);   //Redirect stdin to pipe
 }

 execl("/bin/bash", "/bin/bash", "-c", command.c_str(), NULL);

 close(fd[WRITE]);

 exit(0);
 }
 else
 {
 if (type == "r")
 {
 close(fd[WRITE]); //Close the WRITE end of the pipe since parent's fd is read-only
 }
 else
 {
 close(fd[READ]); //Close the READ end of the pipe since parent's fd is write-only
 }
 }

 pid = child_pid;

 if (type == "r")
 {
 return fdopen(fd[READ], "r");
 }

 return fdopen(fd[WRITE], "w");
 }*/

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

std::string execute_cmd_with_abort(ParallelController* controller, int step,
		std::string command) {
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
			std::cout << ("Abort Step: " + std::to_string(step)) << std::endl;

			kill(-pid, SIGKILL);

			while ((waitpid(-1, &status, WNOHANG)) > 0)
				sleep(1);

			break;
		}
	}

	close(fd);

	return result;
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

long timediff(clock_t t1, clock_t t2) {
	long elapsed;
	elapsed = ((double) t2 - t1) / CLOCKS_PER_SEC * 1000;
	return elapsed;
}

typedef std::vector<std::string> Arguments;

int main(int argc, char *argv[]) {
	Arguments arguments(&argv[0], &argv[0 + argc]);

	if (arguments.size() != 4) {
		std::cout
				<< "missing arguments (use: ./esbmc-parallel filename.c nrprod hmin_value hmax_value)"
				<< std::endl;
		exit(1);
	}

	std::time_t start, end;
	start = std::time(NULL);

	std::string executable = "./esbmc";
	std::string filename = arguments.at(1);
	std::string esbmc_parameters = "--z3 ";

	int hmin = std::atoi(arguments.at(2).c_str());
	int hmax = std::atoi(arguments.at(3).c_str());

	std::string esbmc_version = execute_cmd(executable + " --version");
	size_t nl_version = esbmc_version.find("\n");
	esbmc_version.replace(nl_version, std::string("\n").length(), "");

	std::string execution_date = execute_cmd("date");
	size_t nl_exdate = execution_date.find("\n");
	execution_date.replace(nl_exdate, std::string("\n").length(), "");

	std::string hardware =
			execute_cmd(
					"echo \"CPU:$(cat /proc/cpuinfo | grep \"model name\" | tail -n1 | cut -d \":\" -f2) ($(cat /proc/cpuinfo | grep processor | wc -l) core(s)) ~ RAM: $(cat /proc/meminfo | grep \"MemTotal\" | cut -d \":\" -f2 | rev | cut -d \" \" -f 1,2 | rev)\"");
	size_t nl_hw = hardware.find("\n");
	hardware.replace(nl_hw, std::string("\n").length(), "");

	std::string core_number = execute_cmd(
			"echo \"$(cat /proc/cpuinfo | grep processor | wc -l)\"");
	size_t nl_cn = core_number.find("\n");
	core_number.replace(nl_cn, std::string("\n").length(), "");
	int cores = std::atoi(core_number.c_str());

	std::cout << std::endl;
	std::cout << "*** ESBMC Parallel Runner v2.0 ***" << std::endl;
	std::cout << "Tool: ESBMC " << esbmc_version << std::endl;
	std::cout << "Date of run: " << execution_date << std::endl;
	std::cout << "Hardware: " << hardware << std::endl << std::endl;
	std::cout << "RUNNING:" << std::endl;

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

			long time = 0;
			std::string command_line = executable + " " + filename + " "
					+ esbmc_parameters + " -Dvalordeh=" + std::to_string(step);

			//std::cout << command_line << std::endl;

			std::string result = execute_cmd_with_abort(&controller, step,
					command_line);

			end = std::time(NULL);
			time = end - start;
			std::size_t verification_failed = result.find(
					"VERIFICATION FAILED");
			if (verification_failed != std::string::npos) {
				founds.push_back(step);
				std::cout << std::to_string(step)
						<< " - ESBMC found a violation at VALORDEH using "
						<< thread << " in " << time << "s" << std::endl;

#pragma omp critical
				controller.setStepResult(step, false);
			} else {
#pragma omp critical
				controller.setStepResult(step, true);
			}
		}
	}

	if (founds.size() > 0) {
		int cLower = *std::min_element(founds.begin(), founds.end());

		end = std::time(NULL);
		long time = end - start;

		std::cout << std::endl;
		std::cout << "The best solution is: " << std::to_string(cLower)
				<< " in " << time << "s" << std::endl;
	} else {
		std::cout << std::endl;
		std::cout << "No solution found:(" << std::endl;
	}
}
