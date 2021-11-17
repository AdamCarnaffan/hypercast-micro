call S:\devtools\esp-idf\RUN_SETUP.bat
idf.py build
idf.py -p COM4 flash
idf.py -p COM4 monitor