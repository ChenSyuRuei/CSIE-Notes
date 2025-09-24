#!/usr/bin/env python3
"""
Public Checker for csieNotes Server
Covers Basic and Medium level (fundamental cases only).
Advanced locking and multi-server cases are in private_checker.py
"""

import subprocess
import socket
import time
import shutil
import sys
import os
import threading
import glob
from argparse import ArgumentParser

timeout = 0.1
executable = ["server"]
testpath = "testcases"

parser = ArgumentParser()
parser.add_argument("-t", "--task", choices=["exit", "single_read", "single_update", "invalid"], nargs="+")
args = parser.parse_args()

class Checker():
    def __init__(self):
        self.score = 0
        self.punishment = 0
        self.fullscore = sum(scores)
        self.io = sys.stderr

    def file_miss(self, files):
        return len(set(files) - set(os.listdir(".")))

    def run(self):
        blue("Checking executable file ...", self.io)
        
        # Check if server executable exists
        if self.file_miss(executable):
            red("Server executable not found", self.io)
            exit()

        # Run test cases directly
        for t in testcases:
            self.score += t(self.io)

        self.score = max(0, self.score - self.punishment)
        blue(f"Final score: {round(self.score, 2)} / {self.fullscore}", self.io)

class CsieNotesServer():
    def __init__(self, port):
        self.port = port
        self.p = subprocess.Popen(["./server", str(port)],
                                  stderr=subprocess.DEVNULL,
                                  stdout=subprocess.DEVNULL)
        time.sleep(timeout)

    def exit(self):
        try:
            self.p.terminate()
            self.p.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.p.kill()
            self.p.wait()
        except Exception as e:
            self.p.kill()
            self.p.wait()
            raise Exception(f"Server exit error: {e}")

class CsieNotesClient():
    def __init__(self, port):
        self.port = port
        self.sock = None
        self.connected = False

    def connect(self, timeout=1.0):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(timeout)
            self.sock.connect(("127.0.0.1", self.port))
            self.connected = True
            return True
        except Exception:
            self.connected = False
            return False

    def send_command(self, command):
        try:
            if not self.connected:
                return False
            self.sock.sendall(command.encode())
            return True
        except Exception:
            self.connected = False
            return False

    def receive_response(self, timeout=0.5):
        try:
            if not self.connected:
                return None
            self.sock.settimeout(timeout)
            data = self.sock.recv(4096)
            if data:
                return data.decode("utf-8", errors="ignore")
            else:
                self.connected = False
                return None
        except socket.timeout:
            return ""
        except Exception:
            self.connected = False
            return None

    def disconnect(self):
        try:
            if self.sock:
                self.sock.close()
        except:
            pass
        self.connected = False

def red(str_, io): print("\33[31m" + str_ + "\33[0m", file=io)
def green(str_, io): print("\33[32m" + str_ + "\33[0m", file=io)
def yellow(str_, io): print("\33[33m" + str_ + "\33[0m", file=io)
def blue(str_, io): print("\033[34m" + str_ + "\033[0m", file=io)
def purple(str_, io): print("\033[35m" + str_ + "\033[0m", file=io)

def find_empty_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("localhost", 0))
    _, port = s.getsockname()
    s.close()
    return port


def setup_test_environment(testcase="B1"):
    """Setup test environment with proper files"""
    try:
        # Copy test files
        if os.path.exists(f"{testpath}/{testcase}"):
            if os.path.exists("index"):
                os.remove("index")
            if os.path.exists("note.txt"):
                os.remove("note.txt")
            shutil.copy2(f"{testpath}/{testcase}/index", "./index")
            shutil.copy2(f"{testpath}/{testcase}/note.txt", "./note.txt")
        return True
    except Exception as e:
        print(f"Setup failed: {e}")
        return False

def read_input_file(filepath):
    """Read input commands from .in file"""
    try:
        with open(filepath, 'r') as f:
            content = f.read()
        lines = content.split('\n')
        commands = []
        for line in lines:
            if line.strip(): 
                line = line.replace('\\n', '\n')
                commands.append(line)
        return commands
    except:
        return []

def read_expected_output(filepath):
    """Read expected output from .out file"""
    try:
        with open(filepath, 'r') as f:
            return f.read().strip()
    except:
        return ""

def compare_files(file1, file2):
    """Compare two files and return True if they are identical"""
    try:
        with open(file1, 'r') as f1, open(file2, 'r') as f2:
            content1 = f1.read().strip()
            content2 = f2.read().strip()
            return content1 == content2
    except:
        return False

def execute_commands_on_client(client, commands, command_timeout=1.0):
    """Execute a list of commands on a client and collect responses"""
    responses = []
    for cmd in commands:
        if not client.send_command(cmd):
            break

        if cmd.endswith('\n'):
            cmd_name = cmd.strip().lower()
            if cmd_name == "exit":
                try:
                    client.sock.settimeout(0.5)
                    data = client.sock.recv(1)
                    if data == b"":
                        responses.append("CONNECTION_CLOSED")
                    else:
                        responses.append("EXIT_FAILED_DATA_RECEIVED")
                except socket.timeout:
                    responses.append("EXIT_FAILED_TIMEOUT")
                except (ConnectionResetError, ConnectionAbortedError, OSError) as e:
                    responses.append("CONNECTION_CLOSED")
                except Exception as e:
                    responses.append(f"EXIT_ERROR: {str(e)}")
                break
            else:
                resp = client.receive_response(timeout=command_timeout)
                if resp is not None:
                    responses.append(resp)
                else:
                    responses.append("NO_RESPONSE")
    return responses
    

def execute_testcase_from_file(testcase_name, testcase_dir, io, max_score):
    """Execute a test case using files from testcase directory"""
    purple(f"===== Public Test: {testcase_name} =====", io)
    
    # Check for multiple input files 
    input_pattern = f"{testpath}/{testcase_dir}/testcase-{testcase_dir.lower()}*.in"
    input_files = sorted(glob.glob(input_pattern))
    
    # Fallback to single file pattern if no files found
    if not input_files:
        input_files = [f"{testpath}/{testcase_dir}/testcase-{testcase_dir.lower()}.in"]
    
    expected_output_file = f"{testpath}/{testcase_dir}/testcase-{testcase_dir.lower()}.out"
    expected_note_file = f"{testpath}/{testcase_dir}/expected_note.txt"
    
    # Read all input commands for multiple clients
    all_commands = []
    for input_file in input_files:
        commands = read_input_file(input_file)
        if commands:
            all_commands.append(commands)
        else:
            red(f"|- Failed to read input file: {input_file}", io)
            return 0
    
    if not all_commands:
        red(f"|- No valid input files found", io)
        return 0
    
    expected_output = read_expected_output(expected_output_file)
    
    # Setup test environment with files from this testcase
    if not setup_test_environment(testcase_dir):
        red(f"|- Failed to setup test environment for {testcase_dir}", io)
        return 0
    
    port = find_empty_port()
    server = CsieNotesServer(port)
    current_score = 0
    
    try:
        if len(all_commands) == 1:
            # Single client test
            client = CsieNotesClient(port)
            if not client.connect():
                red("|- Failed to connect to server", io)
                return 0
            
            responses = execute_commands_on_client(client, all_commands[0])
            actual_output = "\n".join([r for r in responses if r and r != "CONNECTION_CLOSED"])
            exit_output = "\n".join(responses)
            client.disconnect()
        else:
            # Multiple client test
            all_responses = []
            threads = []
            
            def client_worker(client_id, commands):
                client = CsieNotesClient(port)
                if client.connect():
                    responses = execute_commands_on_client(client, commands)
                    all_responses.append((client_id, responses))
                    client.disconnect()
                else:
                    all_responses.append((client_id, ["CONNECT_FAILED"]))
            
            # Start all clients concurrently
            for i, commands in enumerate(all_commands):
                thread = threading.Thread(target=client_worker, args=(i, commands))
                threads.append(thread)
                thread.start()
            
            # Wait for all clients to finish with timeout
            start_time = time.time()
            timeout_seconds = 5.0  # 5 seconds timeout for multi-client tests
            
            for thread in threads:
                remaining_time = timeout_seconds - (time.time() - start_time)
                if remaining_time <= 0:
                    red(f"|- Multi-client test timed out after {timeout_seconds} seconds", io)
                    return 0
                thread.join(timeout=remaining_time)
            
            # Combine responses from all clients
            all_responses.sort(key=lambda x: x[0])  # Sort by client_id
            combined_responses = []
            for client_id, responses in all_responses:
                combined_responses.extend(responses)
            
            actual_output = "\n".join([r for r in combined_responses if r and r != "CONNECTION_CLOSED"])
            exit_output = "\n".join(combined_responses)
        # Compare with expected output
        output_matches = True
        if expected_output:
            if expected_output.lower() not in actual_output.lower():
                output_matches = False
                red(f"|- Output mismatch", io)
                yellow(f"   Expected: {expected_output}", io)
                yellow(f"   Actual: {actual_output}", io)
            else:
                yellow(f"|- output matches expected", io)
        else:
            # For tests with empty expected output (like exit test)
            if any("exit" in cmd.lower() for commands in all_commands for cmd in commands):
                # Check if any client properly closed connection
                if "CONNECTION_CLOSED" not in exit_output:
                    output_matches = False
                    # red(f"|- Exit command should close connection", io)
                else:
                    yellow(f"|- Exit command properly closed connection", io)
            else:
                # If no expected output and commands executed without error
                if "NO_RESPONSE" in actual_output:
                    output_matches = False
                    red(f"|- Some commands got no response", io)
                else:
                    yellow(f"|- Commands executed successfully", io)
            
        # Compare note.txt if expected_note.txt exists
        note_matches = True
        if os.path.exists(expected_note_file):
            if os.path.exists("note.txt"):
                if compare_files("note.txt", expected_note_file):
                    yellow(f"|- note matches expected", io)
                else:
                    note_matches = False
                    red(f"|- note.txt: does not match expected", io)
                    # Show file contents for debugging
                    try:
                        with open("note.txt", 'r') as f:
                            actual_note = f.read().strip()
                        with open(expected_note_file, 'r') as f:
                            expected_note = f.read().strip()
                        yellow(f"   Expected note: {expected_note}", io)
                        yellow(f"   Actual note: {actual_note}", io)
                    except:
                        pass
            else:
                note_matches = False
                red(f"|- note.txt: file not found after test", io)
        
        # Determine final score
        if output_matches and note_matches:
            green(f"Public Test: {testcase_name} - passed", io)
            current_score = max_score
        else:
            red(f"Public Test: {testcase_name} - failed", io)
        
        # Disconnect clients (handled in worker threads for multi-client)
        
    except Exception as e:
        red(f"|- Test execution error: {e}", io)
        current_score = 0
    finally:
        server.exit()

    return current_score, exit_output


# ================= TEST CASES =================

def test_exit_command(io):
    """Basic: Exit command closes connection"""
    score, output = execute_testcase_from_file("Exit Command", "B1", io, 0.1)
    # Check if connection was properly closed by server
    if "CONNECTION_CLOSED" not in output:
        red("|- Exit command did not close connection properly", io)
        red(f"   Error: {output}", io)
        return 0
    # Check that we didn't get any error or unexpected responses
    if "EXIT_FAILED" in output or "EXIT_ERROR" in output:
        red("|- Exit command failed or had errors", io)
        red(f"   Output: {output}", io)
        return 0
    return score

def test_single_connection_read(io):
    """Basic: Single server, single client, read commands"""
    score, output = execute_testcase_from_file("Read Command", "B2", io, 0.5)
    return score

def test_single_connection_update(io):
    """Basic: Single server, single client, update commands with same length content change"""
    score, output = execute_testcase_from_file("Update Command", "B3", io, 0.5)
    return score

def test_invalid_command(io):
    """Basic: Invalid command"""
    score, output = execute_testcase_from_file("Invalid Command", "B4", io, 0.2)
    return score

# ================= MAIN EXECUTION =================

# Test configuration
testcases = [
    test_exit_command,
    test_single_connection_read,
    test_single_connection_update,
    test_invalid_command
]
scores = [0.1, 0.5, 0.5, 0.2]

if __name__ == '__main__':
    index = {
        "exit": 0,
        "single_read": 1,
        "single_update": 2,
        "invalid": 3
    }

    if args.task is not None:
        task = []
        for t in args.task:
            task.append(index[t])
        task.sort()
        testcases = [testcases[i] for i in task]
        scores = [scores[i] for i in task]

    Checker().run()