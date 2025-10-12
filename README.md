[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/KwN3W4uF)
## SP2025 HW1

### Spec
[HackMD](https://hackmd.io/@yylunxie/ryRU77Bill)

### Public Judge
###### Usage
```
python3 public_checker.py
```
###### Argument

- `-t TASK [TASK ...]`, `--task TASK [TASK ...]`, Specify which tasks you want to run. If you didn't set this argument, `checker.py` will run all tasks by default.
    - Valid TASK are "exit", "single_read", "single_update", "invalid".
    - for example `python3 checker.py --task exit single_read` will run both `exit` and `single_read`
### Explanation
+ My server opens the `index` and the `note.txt` file every time when it reads a valid command from a client and closes them after finishing the command.
+ If there were any errors in system calls while my server is dealing with commands, it would not shut down immediately but just close the `index` and the `note.txt` file opened for the client and go on for the next one.
+ My server will disconnect some clients whose POLLOUT events are on but I can't write messages back to them.
+ If my server have received a `read` command or the command has been considered invalid, and it have not witten the messages back to the client who issued the command, then it would not accept any `read` command from that client until the previous message has been sent.