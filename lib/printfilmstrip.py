# Produces a PDF for the SLA passed as a parameter.
# Uses the same file name and replaces the .sla extension with .pdf
#
# usage:
# scribus -g -py to-pdf.py file.sla

import os
import time
#print("DEPRECATED since if run via terminal, this script always fails"
#	  " with 'No file is open'"
#      " even if document is specified in same scribus call"
#      " (even though the document opens correctly if python script"
#      " is not specified")
#time.sleep(5)
#exit(1)
if scribus.haveDoc() :
    filename = os.path.splitext(scribus.getDocName())[0]
    pdf = scribus.PDFfile()
    pdf.file = filename+".pdf"
    pdf.save()
else :
    print("[ printfilmstrip.py ] ERROR: No file is open")
