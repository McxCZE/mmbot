(	
	set -e
	mkdir -p new_web
	cd new_web
	wget -q --save-cookies cookies.txt --load-cookies cookies.txt -N https://github.com/Matasx/mmbot-web-ui/releases/latest/download/release.zip
	
	if [ -e files.sha ] && sha1sum --quiet -c files.sha 
	then
	 echo new web is already up to date
	else 
	 echo updating new web pages
	 rm -rf files 
	 (
	 	cd ../www;
	 	find -L . -name . -o -type d -prune -o -type l -exec rm {} +
	 )	 
	 mkdir -p files
	 unzip -qo release.zip -d files
	 (
	 	cd files
	 	sed -i "s/\"url\": *\"\"/\"url\": \"..\\/\"/" config.json
	 	sed -i "s/\"start_url\": *\".\"/\"start_url\":\"index.html\"/" manifest.json
	 )
	 
	 ln -s $PWD/files $PWD/../www/new
	 sha1sum release.zip > files.sha
		echo -----------------------------------------------
		echo "A new user interface has been installed"
		echo 
		echo "Test our new user interface. Open \"/new/index.html\" page"
		echo 
		echo "Send feedback to https://discord.gg/Hs9DbsANeV"
		echo "Credits to Matasx"

	fi
	
)