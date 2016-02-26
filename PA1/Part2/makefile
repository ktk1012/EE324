all: guess_password

guess_password: part2.o guess.o; g++ part2.o guess.o -o guess_password

part2.o: part2.c guess.h; g++ -c $< -o $@

guess.o: guess.c guess.h; g++ -c $< -o $@

clean: rm -rf *.o guess_password


