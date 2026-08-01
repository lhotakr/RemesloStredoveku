using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class Player : MonoBehaviour
{ 

     public static Player Instance {get; private set; }

	 [SerializeField] private float movingSpeed = 4f;

	 private Rigidbody2D rb;

	 public VectorValue startingPosition;
	 private float minMovingSpeed = 0.1f;
	 private bool isRunning = false;
	 [SerializeField] private GameObject attackPoint;

	 [SerializeField] private Animator anim;
	 private float cooldownTimer = Mathf.Infinity;
	 [SerializeField] private float attackCooldown;

	 private void Awake() 
	 {
		 Instance = this;
		 rb = GetComponent<Rigidbody2D>();		 
		 attackPoint.SetActive(false);
		// startingPosition.initialValue = new Vector2(0, 0);
		 transform.position = startingPosition.initialValue;
	 }

	 

	 private void FixedUpdate() 
	 {
		  Vector2 inputVector = new Vector2(0, 0);
		  
		  
		  if (Input.GetKey(KeyCode.W))
		  {
			  inputVector.y = 1f;
		  }
		  if (Input.GetKey(KeyCode.S))
		  {
			  inputVector.y = -1f;
		  }
		  if (Input.GetKey(KeyCode.A))
		  {
			  inputVector.x = -1f;
		  }
		  if (Input.GetKey(KeyCode.D))
		  {
			  inputVector.x = 1f;
		  }
		  if (Mathf.Abs(inputVector.x) > minMovingSpeed || Mathf.Abs(inputVector.y) > minMovingSpeed)
		  {
			  isRunning = true;
			  rb.isKinematic = false;
		  } 
		  else 
		  {
			  isRunning = false;
			  rb.isKinematic = true;
		  }

		  inputVector = inputVector.normalized;
		  rb.MovePosition(rb.position + inputVector * (movingSpeed * Time.fixedDeltaTime));

		  if(Input.GetKey(KeyCode.Mouse0) && cooldownTimer > attackCooldown)
		  {
			  Debug.Log("Mouse");
			  anim.SetTrigger("PlayerAttack");
			  StartCoroutine(AttackCoroutine());
			  cooldownTimer = 0;
		  }
		  
		  cooldownTimer += Time.deltaTime;
	 }

	 private IEnumerator AttackCoroutine()
     {
        yield return new WaitForSeconds(0.5f);
		attackPoint.SetActive(true);
		yield return new WaitForSeconds(0.5f);
		attackPoint.SetActive(false);
	 }

	 public bool IsRunning()
		  {
			  return isRunning;
		  }	 

	
}
