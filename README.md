## SP2025_HW1_release

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
