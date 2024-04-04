<br/>
<p align="center">
  <h3 align="center">C Small Shell</h3>

  <p align="center">
    A simple shell written in C
    <br/>
    <br/>
  </p>
</p>



## About The Project

This shell was created for my CS 344: Operating Systems 1 class as part of an assignment. It features `cd` to change the working directory and can run any command by using `fork()`. It can also read from an input file and output to an output file by using `<` and `>` respectively. 

## Built With

This program was written using C.

## Getting Started

To get a local copy of this project running, follow these steps.

### Prerequisites

This program was written on a Linux machine, so the steps will be geared towards running on a Linux powered machine.

* GCC [(Click to open installation directions)](https://gcc.gnu.org/install/)

### Installation

1. Clone the repo

```sh
git clone https://github.com/aakashmurthy/smallsh.git
```

2. Compile the program

```sh
gcc --std=gnu99 -o smallsh smallsh.c
```

3. Run the compiled program

```sh
./smallsh
```
