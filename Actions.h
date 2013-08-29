//////////////////////////////////////////////////
int DowngradePackage(char *package) {
	tmpchar=NULL;
	int isincache = IsPackageInCache(package); // Here also parsing pacman.log and using flag actions.package_never_upgraded
	if (isincache) {
		if (!quiet_downgrade) printf("Downgrading from Cache, to version %s\n",install_version);
		//printf ("command: %s\n",install_command); //DEBUG
		system(install_command); // Start downgrading from cache
		return 0;
	}
	else { // ELSE Organize checking in  ARM
		int isarmavailable = ReadArm(package);
		if (!isarmavailable) tmpchar = IsPackageInArm(package, install_version);
		if (tmpchar) {
			if (!quiet_downgrade) printf("Downgrade %s from ARM to version %s\n", package,install_version);
				strcpy(install_command,"sudo pacman -U "); strcat(install_command,tmpchar);
				//printf ("command: %s\n",install_command); //DEBUG
				system(install_command);
				return 0;
		}
	}
	if(!quiet_downgrade) {
		if (pkg_never_upgraded==1) printf ("Sorry, package %s can`t be downgrade.\n",package); //Package never upgrades, but installed
		else printf("Package %s not found in AUR, local cache or ARM. Terminating\n", package);
	}
	return 1;
}
//////////////////////////////////////////////////
int GetChoiseForPackage(char *package) {

		int pac_num;
		def_pac=0;
		int ispacmaninit = PacmanInit();
	    if (ispacmaninit) {
			if(!quiet_downgrade) printf("Pacman not initialized! Interrupted\n");
			return 1;
		}
		int ret = CheckDowngradePossibility(package);
		if (ret) return 1;
		ret = ReadArm(package);
		ret = IsPackageInCache(package);
		for (int i=1;i<MAX_PKGS_FROM_ARM_FOR_USER && i<=pkgs_in_arm;i++) {
			printf("%d: %s-%s", i, arm_pkgs[i].name, arm_pkgs[i].version);
			if (!strcmp(arm_pkgs[i].version, installed_pkg_ver)) printf(" [installed]\n");
			else if (!strcmp(arm_pkgs[i].version, install_version)) { printf(" [will be installed by default]\n"); def_pac=i; }
			else printf("\n");
		}
		printf (">> Please enter package number, [q] to quit ");
		if (def_pac>0) printf(", [d] to install default package: ");
		scanf ("%s",package_number);

		return 0;
}
//////////////////////////////////////////////////
int IsPackageInstalled(char *package) {
    const char *local;
    pkg = alpm_db_get_pkg(db_local,package);
    local = alpm_pkg_get_name(pkg);
    if(!local) return 0;// pkg not found in system
    else {
        installed_pkg_ver = alpm_pkg_get_version(pkg); // show pkg version
        return 1;
    }
}
//////////////////////////////////////////////////
int IsPackageInCache(char *package) {
	char *architecture,  full_path_to_packet[200], command[100];
	if(sizeof(void*) == 4) architecture = (char *)"i686";
	else if (sizeof(void*) == 8) architecture = (char *)"x86_64";
	pkg_never_upgraded = 1;
	for (;pacmanlog_length>0;pacmanlog_length--) {
		if (!strcmp(package,pkgs[pacmanlog_length].name) && !strcmp("upgraded",pkgs[pacmanlog_length].action)) { // found necessary package
			if (strcmp(pkgs[pacmanlog_length].cur_version, pkgs[pacmanlog_length].prev_version)) { // if the same version - search next
				strcpy (full_path_to_packet,"/var/cache/pacman/pkg/");
				strcat (full_path_to_packet,package);
				strcat (full_path_to_packet,"-");
				strcat (full_path_to_packet,pkgs[pacmanlog_length].prev_version);
				strcat (full_path_to_packet,"-");
				strcat (full_path_to_packet,architecture);
				strcat (full_path_to_packet,".pkg.tar.xz");
				pkg_never_upgraded = 0; // Package upgraded at least 1 time
				break;
			}
		}
	}
	strcpy(install_version,pkgs[pacmanlog_length].prev_version);
	pFile=fopen(full_path_to_packet,"r");
	if (pFile) {  // previously version available in cache
		strcpy(command,"sudo pacman -U "); // install
		strcat(command,full_path_to_packet);
		strcpy(install_command,command);
		fclose(pFile);
		return 1;
	}
	else return 0;
}
//////////////////////////////////////////////////
static size_t curl_handler(char *data, size_t size, size_t nmemb, void *userp) {

	size_t realsize = size * nmemb;
	struct curl_MemoryStruct *mem = (struct curl_MemoryStruct *)userp;

	mem->memory = realloc(mem->memory, mem->size + realsize + 1);
	memcpy(&(mem->memory[mem->size]), data, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;
	return realsize;
}
//////////////////////////////////////////////////
int IsPackageInAur(char *package) {

	char *name, query[300];
	const char *cont = conte;

	chunk.memory = malloc(1);
	chunk.size = 0;

	curl_global_init(CURL_GLOBAL_ALL);

	curl = curl_easy_init();
	sprintf(query,"https://aur.archlinux.org/rpc.php?type=search&arg=%s",package);
	curl_easy_setopt(curl, CURLOPT_URL, query);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_handler);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
	result = curl_easy_perform(curl);

	//// Parsing AUR response
	cJSON *root = cJSON_Parse(chunk.memory);
	cJSON *item = cJSON_GetObjectItem(root,"results");
	for (int i=0;i<cJSON_GetArraySize(item);i++) {
		cJSON *subitem=cJSON_GetArrayItem(item,i);
		name = cJSON_GetObjectItem(subitem,"Name")->valuestring;
		if (!strcmp(name,package)) return 1; // package in AUR
	}
	cJSON_Delete(root);

	curl_easy_cleanup(curl);
	if(chunk.memory) free(chunk.memory);
	curl_global_cleanup();
  	return 0; // package not in AUR
}
///////////////////////////////////////////////////////
void ReadPacmanLog() {

	action_counter=0;
	char *buff = NULL;
	size_t len;
	char *date, *time, *operat, *pack_name, *cur_version, *prev_version, *fake;
	int i=0;

	pFile=fopen("/var/log/pacman.log","r");
	while (!feof(pFile)) {  // Count lines q-ty in pacman.log
		getline(&buff, &len, pFile);
		action_counter++;
	}
	rewind(pFile);

	pkgs = realloc(pkgs, (action_counter+1) * sizeof(struct packs));

	action_counter=0;
	while (!feof(pFile)) {  // Count lines q-ty in pacman.log
		getline(&buff, &len, pFile);
		date = strtok(buff," ");
		date++;
		time = strtok(NULL,"] ");
		fake = strtok(NULL," ");
		operat = strtok(NULL," ");
		pack_name = strtok(NULL," ");
		//printf("Line: %d, operat: %s\n",i, operat); // DEBUG:
		if (!operat) continue;
		if (!strcmp(operat,"upgraded")) {
			//printf("Upgraded: %s, line: %d\n", pack_name, i+1); //DEBUG:

			prev_version = strtok(NULL," ");
			prev_version++;
			cur_version = strtok(NULL," ");
			cur_version = strtok(NULL,")");
			strcpy(pkgs[action_counter].date,date);
			strcpy(pkgs[action_counter].time,time);
			strcpy(pkgs[action_counter].name,pack_name);
			strcpy(pkgs[action_counter].action,operat);
			strcpy(pkgs[action_counter].cur_version,cur_version);
			strcpy(pkgs[action_counter].prev_version,prev_version);
			action_counter++;
			//printf ("date: %s, time: %s, operat: %s, pack_name: %s\n", date, time, operat, pack_name); //DEBUG
		}
		i++;
	}
	fclose(pFile);
	pacmanlog_length = action_counter;
}
///////////////////////////////////////////////////////
int ReadArm(char *package) {

	char  *str, *last, *architecture, *pointer;
	int counter, counter2;

	if(sizeof(void*) == 4) { architecture = (char *)"i686";  }
	else if (sizeof(void*) == 8) { architecture = (char *)"x86_64"; }

	chunk.memory = malloc(1);
	chunk.size = 0;

	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();
	//sprintf (conte,"http://arm.konnichi.com/search/raw.php?a=%s&q=^%s%24&core=1&extra=1&community=1",architecture,package);
	sprintf (conte,"http://repo-arm.archlinuxcn.org/search?arch=%s&pkgname=%s",architecture,package);
	curl_easy_setopt(curl, CURLOPT_URL, conte);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_handler);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
	result = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	curl_global_cleanup();

	counter2=0;
	pointer = chunk.memory;
	str = strtok(pointer, "\n");
	strcpy(arm_pkgs[counter2].full_path,str);
	counter2++;
	for (;str = strtok(NULL, "\n"); counter2++) {
		strcpy(arm_pkgs[counter2].full_path,str);
	}
	pkgs_in_arm = counter2;
	counter=0;

	int l=0, i=1;
	char full[1000];
	while (l<MAX_PKGS_FROM_ARM_FOR_USER) { // Get info about packages in ARM
		if (!strlen(arm_pkgs[l].full_path)) break;
		strcpy(full,arm_pkgs[l].full_path);
		str = strtok(full, "|");
		if (strcmp(str,"testing")) { // Exclude packages from `testing`
			strcpy(arm_pkgs[i].repository,str);
			//printf("%d: Repo: %s",i, arm_pkgs[i].repository);
			str = strtok(NULL, "|");
			strcpy(arm_pkgs[i].name,str);
			//printf(", name: %s",arm_pkgs[i].name);
			str = strtok(NULL, "|");
			str = strtok(NULL, "|");
			strcpy(arm_pkgs[i].version,str);
			//printf(", version: %s",arm_pkgs[i].version);
			str = strtok(NULL, "|");
			strcpy(arm_pkgs[i].link,str);
			//printf(", link: %s\n",arm_pkgs[i].link);
			str = strtok(NULL, "|");
			strcpy(arm_pkgs[i].pkg_release,str);
			i++;
		}
		l++;
	}
	pkgs_in_arm = i-1; // finally packages q-ty in ARM
	if(!quiet_downgrade) printf("Packages in ARM: %d\n",pkgs_in_arm);

	if(chunk.memory) free(chunk.memory);

return 0;
}
////////////////////////////////////////////////////////////////
char* IsPackageInArm(char *package, char *version) {
	int arm_flag=0;
	char t_pack[100];
	sprintf(t_pack,"%s-%s",package,version);
	for(tmpint=0;strlen(arm_pkgs[tmpint].full_path)>0;tmpint++) {
		//printf("%s\n",arm_packages[tmpint].full_path); // DEBUG
		if (strstr(arm_pkgs[tmpint].full_path,t_pack)) {
			arm_flag=1;
			break;
		}
	}
	if (arm_flag==1) return arm_pkgs[tmpint].link;
	else return NULL;
}
////////////////////////////////////////////////////////////////
int CheckDowngradePossibility(char *package) {
	int isinstalled = IsPackageInstalled(package);
	if (!isinstalled) {
		if(!quiet_downgrade) printf("Package '%s' not installed.\n", package);
		return 1;
	}
	int isinaur = IsPackageInAur(package);
	if (isinaur) {
		if(!quiet_downgrade) printf("Package '%s' in AUR. Downgrade impossible.\n", package);
		return 1;
	}
return 0;
}
////////////////////////////////////////////////////////////////
int PacmanInit() {

	pkgs = calloc(1, sizeof(struct packs));
	arm_pkgs = calloc(1, sizeof(struct arm_packs));
	arm_pkgs = realloc(arm_pkgs, MAX_PKGS_FROM_ARM_FOR_USER*sizeof(struct arm_packs));

    alpm_handle = NULL;
    alpm_handle = alpm_initialize("/","/var/lib/pacman/",0);
    if(!alpm_handle) {
        printf("Libalpm initialize error!\n");
        return 1;
    }
    db_local = alpm_get_localdb(alpm_handle);
    ReadPacmanLog();

    return 0;
}
int PacmanDeinit() {
	free(pkgs);
	free(arm_pkgs);
	alpm_release(alpm_handle);
}
