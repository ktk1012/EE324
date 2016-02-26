all: guess_password

guess_password: part1.o guess.o; g++ part1.o guess.o -o guess_password

part1.o: part1.c guess.h; g++ -c $< -o $@

guess.o: guess.c guess.h; g++ -c $< -o $@

clean: rm -rf *.o guess_password
