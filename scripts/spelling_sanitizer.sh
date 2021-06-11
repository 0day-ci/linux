#!/bin/sh

src=spelling.txt
tmp=spelling_mistake_correction_pairs.txt

cd `dirname $0`

# Convert the format of 'codespell' to the current
sed -r -i 's/ ==> /||/' $src

# Move the spelling "mistake||correction" pairs into file $tmp
# There are currently 9 lines of comments in $src, so the text starts at line 10
sed -n '10,$p' $src > $tmp
sed -i '10,$d' $src

# Remove duplicates first, then sort by correctly spelled words
sort -u $tmp -o $tmp
sort -t '|' -k 3 $tmp -o $tmp

# Append sorted results to comments
cat $tmp >> $src

# Delete the temporary file
rm -f $tmp

cd - > /dev/null
