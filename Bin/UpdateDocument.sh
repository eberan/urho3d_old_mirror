cd $( dirname $0 )
echo "Dumping Script API..."
./ScriptCompiler -dumpapi ../Docs/ScriptAPI.dox
if [ $? -ne 0 ]; then exit 1; fi
echo "Converting Doxygen files to Wiki..."
./DocConverter ../Docs ../../wiki Urho3D
if [ "$1" != "-a" ]; then exit 0; fi
echo "Converting Doxygen files to HTML..."
cd .. && doxygen Doxyfile 1>/dev/null
echo "Finish."
