enum Update_Parameters {
	BEFORE_MAIN_LOOP,
	BEFORE_FIRST_KICK,
	AFTER_FIRST_KICK,
	BEFORE_DRIFT,
	AFTER_DRIFT,
	AFTER_NEW_TIMESTEPS,
	BEFORE_SECOND_KICK,
	AFTER_SECOND_KICK
};

void Update(enum Update_Parameters stage); 
