// wbmsx - minimal native QuickBMS script interpreter.
//
// Thin front end: the interpreter lives in lib-bms.c and is shared with
// `wszst BMS`. No quickbms binary is invoked; scripts run in-process.

#include <stdio.h>
#include <string.h>
#include "lib-std.h"
#include "lib-bms.h"

int main (int argc, char **argv)
{
	if (argc < 4)
	{
		printf ("wbmsx - minimal native QuickBMS script interpreter\n"
				"Also available as 'wszst BMS'.\n\n"
				"Usage: %s <script.bms> <input_file> <output_dir>\n"
				"Supports: IDSTRING GET GETDSTRING GOTO SAVEPOS MATH SET\n"
				"          FOR/NEXT IF/ELSE/ENDIF LOG CLOG COMTYPE ENDIAN PRINT\n",
			argv[0]);
		return ERR_SYNTAX;
	}
	return RunBmsScript (argv[1], argv[2], argv[3]);
}

bool DefineIntVar (VarMap_t *vm, ccp varname, int value)
{
	return false;
}
