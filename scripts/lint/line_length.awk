#!/usr/bin/awk -f

{
	gsub(/\t/, "        ")
	if (length() > 80) {
		print "line too long: " NR
		exitcode = 1
	}
}
END { exit exitcode }
