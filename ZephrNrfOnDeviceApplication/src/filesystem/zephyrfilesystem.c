#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/rand32.h>
#include <time.h>


#include <stdlib.h>
#include "zephyrfilesystem.h"


LOG_MODULE_REGISTER(zephyrfilesystem);

#if CONFIG_DISK_DRIVER_FLASH
#include <zephyr/storage/flash_map.h>
#endif

#if CONFIG_FAT_FILESYSTEM_ELM
#include <ff.h>
#endif

#if CONFIG_FILE_SYSTEM_LITTLEFS
#include <zephyr/fs/littlefs.h>
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
#endif

#define STORAGE_PARTITION		storage_partition
#define STORAGE_PARTITION_ID		FIXED_PARTITION_ID(STORAGE_PARTITION)

#define MAX_BUFFER_SIZE 500


// Might need to put this and the timer in a seperate file.

struct k_work_q my_work_q;



//data limit per file in bytes
static int data_limit = MAX_BUFFER_SIZE;
static int create_newfile_limit = 500;



// external globals
int storage_percent_full;

memory_container work_item;


uint64_t last_time_update_sent;

uint64_t set_date_time = 0;

int patient_num = 0;

int packet_number = 0;

int last_packet_number_processed = 0;

static int current_file_count;

typedef struct k_sensor_upload {
	enum sensor_type sensor;
	struct k_work work;
};


typedef struct data_upload_buffer {
	char data_upload_buffer[MAX_BUFFER_SIZE];
	size_t current_size;
} data_upload_buffer;


// settings
bool use_random_files = false;
bool direct_write_file = true; 




// internally linked globals
static struct fs_mount_t fs_mnt;
//counter to serve as a amount for when the file fills up.
static int data_counter;
char file_name[50] = "";
static bool first_write = false;
static struct fs_file_t file;


typedef struct MotionSenseFile {
	int max_size;
	int data_counter;
	char sensor_string[3];
	char file_name[50];
	struct fs_file_t self_file;
	bool first_write;
	bool switch_buffer;
	data_upload_buffer buffer1;
	data_upload_buffer buffer2;
	
} 	MotionSenseFile;



//File Objects
static MotionSenseFile current_file;

MotionSenseFile ppg_file;

MotionSenseFile accel_file = {
	.max_size = 840,
	.sensor_string = "ac"
};





void create_test_files(){
	printk("trying to write files...\n");
	struct fs_file_t test_file;
	fs_file_t_init(&test_file);
	char destination[50] = "";
	struct fs_mount_t* mp = &fs_mnt;
	strcat(destination, mp->mnt_point);
	strcat(destination, "/test.txt"); 
	int file_create = fs_open(&test_file, destination, FS_O_CREATE | FS_O_WRITE);
	if (file_create == 0){
		char a[] = "hello world";
		printk("trying to write...\n");
		fs_write(&test_file, a, sizeof(a));
		printk("done writing\n");
		fs_close(&test_file);
	}
}


void sensor_write_to_file(const void* data, size_t size, enum sensor_type sensor){
	struct fs_mount_t* mp = &fs_mnt;
	MotionSenseFile* MSenseFile;

	if (sensor == ppg){
		MSenseFile = &ppg_file;
	}
	else if (sensor == accelorometer){
		MSenseFile = &accel_file;
	}


	if (true){
		
		fs_file_t_init(&MSenseFile->self_file);
		
		
		int ID = 0;
		char IDString[5];
		char patient_id[6];
		if (use_random_files){
			
		
			ID = sys_rand32_get() % 900;
			
		}
		else {
			uint64_t current_time = get_current_unix_time();
			ID = current_time;

		}
		itoa(ID, IDString,  10);
		

		memset(MSenseFile->file_name, 0, sizeof(MSenseFile->file_name));
		strcat(MSenseFile->file_name, mp->mnt_point);
		strcat(MSenseFile->file_name, "/");
		if (patient_num != 0){
			itoa(patient_num, patient_id, 10);
			strcat(MSenseFile->file_name, patient_id);	
		}
		strcat(MSenseFile->file_name, MSenseFile->sensor_string);
		strcat(MSenseFile->file_name, IDString);
		strcat(MSenseFile->file_name, ".bin");
		//printk("file: %s \n", file_name); 
		int file_create = fs_open(&MSenseFile->self_file, MSenseFile->file_name, FS_O_CREATE | FS_O_WRITE);
		first_write = true;

	}
	else if (data_counter >= data_limit){
		//memset(file_name, 0, sizeof(file_name));
		data_counter = 0;
	}
	
	int total_written = fs_write(&MSenseFile->self_file, data, size);
	//fs_write(&file, data, size);
	if (total_written = size){
		//printk("sucessfully wrote file, bytes written = %i ! \n", total_written);
		data_counter += total_written;
	}
	fs_close(&MSenseFile->self_file);
}


void write_to_file(const void* data, size_t size){
	struct fs_mount_t* mp = &fs_mnt;
	if (!first_write ){
		
		fs_file_t_init(&file);
		
		
		int ID = 0;
		char IDString[5];
		if (use_random_files){
			
		
			ID = sys_rand32_get() % 900;
			
		}
		else {
			uint64_t current_time = get_current_unix_time();
			ID = current_time;

		}
		itoa(ID, IDString,  10);

		

		strcat(file_name, mp->mnt_point);
		strcat(file_name, "/");
		strcat(file_name, IDString);
		strcat(file_name, "test.txt");
		//printk("file: %s \n", file_name); 
		int file_create = fs_open(&file, file_name, FS_O_CREATE | FS_O_WRITE);
		first_write = true;

	}
	else if (data_counter >= data_limit){
		//memset(file_name, 0, sizeof(file_name));
		data_counter = 0;
	}
	
	int total_written = fs_write(&file, data, size);
	//fs_write(&file, data, size);
	if (total_written = size){
		//printk("sucessfully wrote file, bytes written = %i ! \n", total_written);
		data_counter += total_written;
	}
}




void work_write(struct k_work* item){
	
	memory_container* container =
        CONTAINER_OF(item, memory_container, work);
	
	sensor_write_to_file(container->address, container->size, container->sensor);
	// packets should always be in FIFO order for the queue, for sake of the data order. This check makes sure this is always ensured.
	if (container->packet_num <= last_packet_number_processed){
		LOG_ERR("FIFO in k_work not met.");	
	}
	LOG_INF("Processing packet %i", container->packet_num);
	last_packet_number_processed = container->packet_num;

}

void submit_write(const void* data, size_t size, enum sensor_type type){
	
	//memcpy(work_item.address, data, size);
	work_item.address = data;
	work_item.size = size;
	work_item.sensor = type;
	packet_number++;
	work_item.packet_num = packet_number;
	int ret = k_work_submit_to_queue(&my_work_q, &work_item.work);
	if (ret != 1){
		LOG_ERR("bad ret value: %i", ret);
	}
	LOG_INF("ret value: %i", ret);

}

void store_data(const void* data, size_t size, enum sensor_type sensor){
	LOG_DBG("Store data called");
	data_upload_buffer* current_buffer;
	int16_t arr[6];
	MotionSenseFile* MSenseFile;
	if (sensor == ppg){
		MSenseFile = &ppg_file;
	}
	else if (sensor == accelorometer){
		MSenseFile = &accel_file;
	}

	if (MSenseFile->switch_buffer){
		current_buffer = &MSenseFile->buffer2;
	}
	else {
		current_buffer = &MSenseFile->buffer1;
	}

	void* address_to_write = &current_buffer->data_upload_buffer[current_buffer->current_size];
	void* result = memcpy(address_to_write, data, size);
	memcpy(arr, address_to_write, size);
	current_buffer->current_size += size;
	if (current_buffer->current_size >= MSenseFile->max_size){
		if (current_buffer->current_size != MSenseFile->max_size){
			LOG_WRN("Warning: size of total buffer is overflowing from last, truncating...");
		}
		LOG_INF("Submitting File!");
		submit_write(current_buffer->data_upload_buffer, MSenseFile->max_size, sensor);
		current_buffer->current_size = 0;
		MSenseFile->switch_buffer = !MSenseFile->switch_buffer;
	}
}

int close_all_files(){

	int code = fs_close(&file);
	return code;

}


static int setup_flash(struct fs_mount_t *mnt)
{
	int rc = 0;
#if CONFIG_DISK_DRIVER_FLASH
	unsigned int id;
	const struct flash_area *pfa;

	mnt->storage_dev = (void *)STORAGE_PARTITION_ID;
	id = STORAGE_PARTITION_ID;

	rc = flash_area_open(id, &pfa);
	printk("Area %u at 0x%x on %s for %u bytes\n",
	       id, (unsigned int)pfa->fa_off, pfa->fa_dev->name,
	       (unsigned int)pfa->fa_size);

	if (rc < 0 && IS_ENABLED(CONFIG_APP_WIPE_STORAGE)) {
		printk("Erasing flash area ... ");
		rc = flash_area_erase(pfa, 0, pfa->fa_size);
		printk("%d\n", rc);
	}

	if (rc < 0) {
		flash_area_close(pfa);
	}
#endif
	return rc;
}

static int mount_app_fs(struct fs_mount_t *mnt)
{
	int rc;

#if CONFIG_FAT_FILESYSTEM_ELM
	static FATFS fat_fs;

	mnt->type = FS_FATFS;
	mnt->fs_data = &fat_fs;
	if (IS_ENABLED(CONFIG_DISK_DRIVER_RAM)) {
		mnt->mnt_point = "/RAM:";
	} else if (IS_ENABLED(CONFIG_DISK_DRIVER_SDMMC)) {
		mnt->mnt_point = "/SD:";
	} else {
		mnt->mnt_point = "/NAND:";
	}

#elif CONFIG_FILE_SYSTEM_LITTLEFS
	mnt->type = FS_LITTLEFS;
	mnt->mnt_point = "/lfs";
	mnt->fs_data = &storage;
#endif
	rc = fs_mount(mnt);

	return rc;
}

void setup_disk(void)
{
	struct fs_mount_t *mp = &fs_mnt;
	struct fs_dir_t dir;
	struct fs_statvfs sbuf;
	int rc;

	fs_dir_t_init(&dir);

	if (IS_ENABLED(CONFIG_DISK_DRIVER_FLASH)) {
		rc = setup_flash(mp);
		if (rc < 0) {
			LOG_ERR("Failed to setup flash area");
			return;
		}
	}

	if (!IS_ENABLED(CONFIG_FILE_SYSTEM_LITTLEFS) &&
	    !IS_ENABLED(CONFIG_FAT_FILESYSTEM_ELM)) {
		LOG_INF("No file system selected");
		return;
	}

	rc = mount_app_fs(mp);
	if (rc < 0) {
		LOG_ERR("Failed to mount filesystem");
		return;
	}
	/* Allow log messages to flush to avoid interleaved output */
	k_sleep(K_MSEC(50));

	printk("Mount %s: %d\n", fs_mnt.mnt_point, rc);

	rc = fs_statvfs(mp->mnt_point, &sbuf);
	if (rc < 0) {
		printk("FAIL: statvfs: %d\n", rc);
		return;
	}

	printk("%s: bsize = %lu ; frsize = %lu ;"
	       " blocks = %lu ; bfree = %lu\n",
	       mp->mnt_point,
	       sbuf.f_bsize, sbuf.f_frsize,
	       sbuf.f_blocks, sbuf.f_bfree);

	rc = fs_opendir(&dir, mp->mnt_point);
	printk("%s opendir: %d\n", mp->mnt_point, rc);

	if (rc < 0) {
		LOG_ERR("Failed to open directory");
	}

	while (rc >= 0) {
		struct fs_dirent ent = { 0 };

		rc = fs_readdir(&dir, &ent);
		if (rc < 0) {
			LOG_ERR("Failed to read directory entries");
			break;
		}
		if (ent.name[0] == 0) {
			printk("End of files\n");
			break;
		}
		printk("  %c %u %s\n",
		       (ent.type == FS_DIR_ENTRY_FILE) ? 'F' : 'D',
		       ent.size,
		       ent.name);
	}

	(void)fs_closedir(&dir);

	return;
}


int get_storage_percent_full(){
	struct fs_statvfs info;
	struct fs_mount_t* mp = &fs_mnt;
	int rc = fs_statvfs(mp->mnt_point, &info);
	if (rc < 0) {
		printk("FAIL: statvfs: %d\n", rc);
		return;
	}

	printk("%s: bsize = %lu ; frsize = %lu ;"
	       " blocks = %lu ; bfree = %lu\n",
	       mp->mnt_point,
	       info.f_bsize, info.f_frsize,
	       info.f_blocks, info.f_bfree);

	float storage_percent = (info.f_blocks - info.f_bfree);
	storage_percent /= info.f_blocks;
	storage_percent *= 100;
	storage_percent_full = (int)storage_percent;
	LOG_INF("storage: %f and %i", storage_percent, storage_percent_full);
	return (int)storage_percent;

}


void set_date_time_bt(uint64_t value){
	
	set_date_time = value;
	last_time_update_sent = k_uptime_get() / 1000;
	LOG_INF("new datetime sent, value is %llu, seconds uptime is %i", set_date_time, last_time_update_sent);
	

}

uint64_t get_current_unix_time(){
	
	uint64_t current_upime = k_uptime_get();
	current_upime /= 1000;
	LOG_INF("current uptime in seconds: %llu", current_upime);
	uint64_t current_time = (current_upime - last_time_update_sent) + set_date_time;
	LOG_INF("current timestamp: %llu", current_time);
	return current_time;
}


int64_t start_time;

void start_timer(){
	start_time = k_uptime_get();
}


int64_t stop_timer(){
	int64_t length = k_uptime_get() - start_time;
	start_time = 0;
	LOG_INF("Timer Value: %i ms", length);
	return length;
}

